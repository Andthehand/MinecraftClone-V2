#include "Renderer.h"
#include "ChunkMesh.h"
#include "VoxelTextures.h"
#include "VoxelShadowVolume.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <glm/vec4.hpp>
#include <glm/matrix.hpp>
#include <iostream>
#include <vector>

namespace {
	// Returns a list of chunk offsets to load, sorted by distance from the origin.
    const std::vector<ChunkCoord>& GetChunkLoadOffsets() {
        static const std::vector<ChunkCoord> offsets = [] {
            std::vector<ChunkCoord> result;
            constexpr int radius = Options::World::LOAD_RADIUS;
            for (int z = -radius; z <= radius; ++z) {
                for (int y = -radius; y <= radius; ++y) {
                    for (int x = -radius; x <= radius; ++x) {
                        result.push_back({ x, y, z });
                    }
                }
            }

            std::stable_sort(result.begin(), result.end(), [](ChunkCoord a, ChunkCoord b) {
                return World::Distance(a, {0, 0, 0}) < World::Distance(b, {0, 0, 0});
            });
            return result;
        }();
        return offsets;
    }

    struct VoxelParameters {
        glm::mat4 viewProjection;
        glm::vec4 chunkOrigin;
        glm::vec4 cameraPositionFogStart;
        float fogEnd;
        uint32_t textureIndex;
        uint32_t samplerIndex;
        uint32_t vertexBufferIndex;
        uint32_t sunDataIndex;
        uint32_t voxelShadowIndex;
    };

    struct SunData {
        glm::vec4 sunDirection;
        glm::vec4 raySettings; // Local radius, blend start, ray length, enabled.
    };

    struct SkyParameters {
        glm::mat4 inverseViewProjection;
        glm::vec4 sunDirectionSize; // Normalized direction and tangent of angular half-size.
        glm::vec4 sunRight; // Orbit axis keeps the disc orientation stable through noon.
    };
}

Renderer::Renderer(Device& device, Swapchain& swapchain)
    : m_Device(device), m_Swapchain(swapchain) {
    const auto pixels = VoxelTextures::BuildAtlas();
    if (device.CreateTexture({
            .width = VoxelTextures::WIDTH, .height = VoxelTextures::HEIGHT,
            // Atlas bytes are display colors; decode them before lighting.
            .format = TextureFormat::RGBA8_SRGB,
            .usage = TextureUsage::ShaderResource | TextureUsage::TransferDst,
            .debugName = "VoxelAtlas",
        }, m_VoxelAtlas) != Result::Success ||
        m_VoxelAtlas->UploadData(pixels.data(), static_cast<uint32_t>(pixels.size())) != Result::Success ||
        device.CreateSampler({
            .minFilter = Filter::Nearest, .magFilter = Filter::Nearest, .mipFilter = Filter::Nearest,
            .addressU = SamplerAddressMode::ClampToEdge, .addressV = SamplerAddressMode::ClampToEdge,
            .debugName = "VoxelPixelSampler",
        }, m_VoxelSampler) != Result::Success) {
        std::cerr << "Failed to create voxel textures.\n";
        return;
    }
    if (!CreateVoxelPipeline() || !CreateSkyPipeline()) {
        return;
    }

    m_Pool = std::make_unique<TransientResourcePool>(device, Options::Rendering::TRANSIENT_POOL_BYTES);
}

bool Renderer::CreateVoxelPipeline() {
    Ref<Shader> voxelShader;
    if (m_Device.CreateShader({ .moduleName = Options::Rendering::SHADER_MODULE,
                            .debugName = "VoxelRasterShader" }, voxelShader) != Result::Success) {
        std::cerr << "Failed to create voxel raster voxelShader.\n";
        return false;
    }

    PipelineDesc pipelineDesc{
        .shader = voxelShader.Raw(),
        .rasterState = { .cullMode = CullMode::Front, .fillMode = FillMode::Solid },
        .depthState = { .depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less },
        .renderTargetFormats = { .colorFormats = { m_Swapchain.GetBackBufferFormat() }, .depthFormat = TextureFormat::D32_Float },
        .debugName = "VoxelSolidPipeline",
    };
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_SolidPipeline) != Result::Success) {
        std::cerr << "Failed to create solid voxel pipeline.\n";
        return false;
    }
    pipelineDesc.rasterState.fillMode = FillMode::Wireframe;
    pipelineDesc.debugName = "VoxelWireframePipeline";
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_WireframePipeline) != Result::Success) {
        std::cerr << "Failed to create wireframe voxel pipeline.\n";
        return false;
    }

    return true;
}

bool Renderer::CreateSkyPipeline() {
    Ref<Shader> skyShader;
    if (m_Device.CreateShader({ .moduleName = Options::Rendering::SKY_SHADER_MODULE,
                            .debugName = "SkyShader" }, skyShader) != Result::Success) {
        std::cerr << "Failed to create sky voxelShader.\n";
        return false;
    }

    PipelineDesc pipelineDesc{
        .shader = skyShader.Raw(),
        .rasterState = { .cullMode = CullMode::None },
        .depthState = { .depthTestEnable = false, .depthWriteEnable = false },
        .renderTargetFormats = { .colorFormats = { m_Swapchain.GetBackBufferFormat() }, .depthFormat = TextureFormat::D32_Float },
        .debugName = "SkyPipeline",
    };
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_SkyPipeline) != Result::Success) {
        std::cerr << "Failed to create sky pipeline.\n";
        return false;
    }

    return true;
}

bool Renderer::UpdateChunks(glm::vec3 position) {
    const ChunkCoord player = World::ChunkCoordinate(position);
    for (auto it = m_World.chunks.begin(); it != m_World.chunks.end();) {
        if (World::Distance(it->first, player) > Options::World::UNLOAD_RADIUS) {
            // Render graph resource uses retain the buffers until GPU completion.
            it = m_World.chunks.erase(it);
        }
        else {
            ++it;
        }
    }

    uint32_t loaded = 0;
    for (const auto offset : GetChunkLoadOffsets()) {
        if (loaded == Options::World::CHUNKS_LOADED_PER_FRAME) {
            break;
        }

        const ChunkCoord coord = player + offset;
        if (m_World.chunks.contains(coord)) {
			// Already loaded, skip...
            continue;
        }

        auto it = m_World.chunks.try_emplace(coord).first;
        auto& chunk = it->second;
        m_World.Generate(chunk.octree, coord);
        loaded++;
    }

    // Border chunks hold voxel data only. Wait for all 26 neighbors before
    // building a mesh, then keep it cached as neighbors stream in and out.
    for (auto& [coord, chunk] : m_World.chunks) {
        if (!chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) {
			// Not ready to mesh yet, skip...
            continue;
        }

        const ChunkMesh mesh = BuildChunkMesh(chunk.octree, m_World.GetChunkNeighbors(coord));
        Ref<Buffer> vertices;
        Ref<Buffer> indices;
        if (!mesh.indices.empty()) {
            if (m_Device.CreateBuffer({
                    .size = mesh.vertices.size() * sizeof(VoxelVertex),
                    .usage = BufferUsage::Storage,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.vertices.data(), 
                    .debugName = "ChunkVertices",
                }, vertices) != Result::Success ||
                m_Device.CreateBuffer({
                    .size = mesh.indices.size() * sizeof(uint32_t),
                    .usage = BufferUsage::Index,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.indices.data(), 
                    .debugName = "ChunkIndices",
                }, indices) != Result::Success) {
                std::cerr << "Failed to upload chunk mesh.\n";

                return false;
            }
        }

        // Publish only a complete upload, also clearing buffers for empty meshes.
        chunk.vertices = std::move(vertices);
        chunk.indices = std::move(indices);
        chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());
        chunk.meshDirty = false;
    }

    return true;
}

struct Renderer::FrameResources {
    struct VisibleChunk {
        ChunkCoord coord;
        World::Chunk* chunk;
    };
    std::vector<VisibleChunk> visibleChunks;
    RenderGraphResource sunBuffer;
    std::vector<RenderGraphUse> geometry;
    glm::vec3 sunDirection;
    RenderGraphResource voxelVolume;
    bool voxelShadows = false;
};

bool Renderer::UpdateVoxelShadowVolume(glm::vec3 position) {
    const ChunkCoord center = World::ChunkCoordinate(position);
    if (m_VoxelShadowVolume && center == m_VoxelShadowCenter) {
		// Voxel shadow volume is already up-to-date for the current player position, no need to rebuild.
        return true;
    }

    const auto current = m_World.chunks.find(center);
    if (current == m_World.chunks.end()) {
		// The center chunk is not loaded yet, so we cannot build a shadow volume.
        return true;
    }

    auto currentChunkNeighbors = m_World.GetChunkNeighbors(center);
    currentChunkNeighbors[ChunkNeighborIndex(0, 0, 0)] = &current->second.octree;
    for (const auto* chunk : currentChunkNeighbors) {
        if (!chunk) {
			// Not all neighbors are loaded yet, so we cannot build a shadow volume.
            return true;
        }
    }

    const auto shadowVolume = VoxelShadows::VoxelShadowVolume::Build(currentChunkNeighbors, (center - ChunkCoord(1)) * int(Options::World::CHUNK_SIZE));
    const auto uploadData = shadowVolume.GetUploadData();
    Ref<Buffer> volume;
    if (m_Device.CreateBuffer({.size = uploadData.size_bytes(), .usage = BufferUsage::Storage,
            .memoryUsage = MemoryUsage::CPUToGPU, .initialData = uploadData.data(),
            .debugName = "LocalVoxelShadowOccupancy"}, volume) != Result::Success) {
        std::cerr << "Failed to upload voxel shadow occupancy.\n";
        return false;
    }

    // Immutable snapshots remain retained by in-flight graph submissions.
    m_VoxelShadowVolume = std::move(volume);
    m_VoxelShadowCenter = center;

    return true;
}

void Renderer::CollectVisibleChunks(RenderGraph& graph, FrameResources& resources, ChunkCoord player,
    const Camera& camera) {
    const auto planes = camera.GetFrustumPlanes();
    auto& uses = resources.geometry;
    for (auto& [coord, chunk] : m_World.chunks) {
        if (chunk.indexCount == 0 || chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) {
			// Not ready to mesh yet, skip...
            continue;
        }

        const glm::vec3 minimum = glm::vec3(coord) * float(Options::World::CHUNK_SIZE);
        if (!Camera::IsBoxVisible(planes, minimum, minimum + glm::vec3(float(Options::World::CHUNK_SIZE)))) {
            continue;
        }

        resources.visibleChunks.push_back({coord, &chunk});
        uses.push_back({graph.ImportBuffer(chunk.vertices.Raw()),
            {PipelineStage::VertexShader, ResourceAccess::ShaderRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
        uses.push_back({graph.ImportBuffer(chunk.indices.Raw()),
            {PipelineStage::VertexInput, ResourceAccess::IndexRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
    }

}

void Renderer::AddSunDataPass(RenderGraph& graph, FrameResources& resources) {
    const SunData sunData{
        .sunDirection = glm::vec4(resources.sunDirection, 0),
        .raySettings = {Options::Rendering::VOXEL_SHADOW_FADE_END_DISTANCE, Options::Rendering::VOXEL_SHADOW_FADE_START_DISTANCE,
            Options::Rendering::VOXEL_SHADOW_MAX_RAY_LENGTH, resources.voxelShadows ? 1.0f : 0.0f},
    };
    resources.sunBuffer = graph.CreateBuffer({
        .size = sizeof(SunData), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .debugName = "SunLightingData",
    });

    graph.AddPass({
        .name = "InitializeSunData",
        .resources = {{resources.sunBuffer, {PipelineStage::AllCommands, ResourceAccess::MemoryWrite},
                        TextureLayout::Undefined, RenderGraphAccess::Write},
    }}, [&graph, &resources, sunData](CommandList& commands) {
        auto* buffer = graph.GetBuffer(resources.sunBuffer);
        commands.UseResource(buffer);

        if (buffer->WriteData(&sunData, sizeof(sunData)) != Result::Success) {
            std::cerr << "Failed to write sun lighting data.\n";
        }
    });
}

void Renderer::AddVoxelPass(RenderGraph& graph, const FrameResources& resources,
    const FrameContext& frame, const Camera& camera) {
    const auto backbuffer = graph.ImportBackBuffer(m_Swapchain, frame);
    const auto depth = graph.CreateTexture({
        .width = frame.width, .height = frame.height,
        .format = TextureFormat::D32_Float, 
        .usage = TextureUsage::DepthStencil,
        .debugName = "VoxelDepth",
    });

    // Resource states
	constexpr ResourceState backbufferWrite{ PipelineStage::ColorAttachmentOutput, ResourceAccess::ColorAttachmentWrite };
	constexpr ResourceState depthWrite{
        PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
		ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite
    };
    constexpr ResourceState sampled{ PipelineStage::FragmentShader, ResourceAccess::ShaderRead };

	// Track all resources used in the voxel pass, including geometry and shadow data.
    auto uses = resources.geometry;
    uses.push_back({backbuffer, backbufferWrite, TextureLayout::ColorAttachment, RenderGraphAccess::Write});
    uses.push_back({depth, depthWrite, TextureLayout::DepthStencilAttachment, RenderGraphAccess::Write});
    uses.push_back({resources.sunBuffer, sampled, TextureLayout::Undefined, RenderGraphAccess::Read});
    if (resources.voxelShadows) {
        uses.push_back({ resources.voxelVolume, sampled, TextureLayout::Undefined, RenderGraphAccess::Read });
    }
    uses.push_back({graph.ImportTexture(m_VoxelAtlas.Raw(), TextureLayout::ShaderRead, sampled),
        sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});

    graph.AddPass({.name = "RasterizeVoxels.VoxelRays", .resources = std::move(uses)},
        [this, &graph, &resources, frame, camera, depth](CommandList& commands) {
            commands.BeginRendering({
                .colorAttachments = {{.target = frame.backBufferView, .clearColor = {0.45f, 0.65f, 0.9f, 1.0f}}},
                .hasDepth = true,
                .depthAttachment = {.target = graph.GetTexture(depth)->GetTextureView()},
                .renderArea = {.width = frame.width, .height = frame.height},
        });

        // Set dynamic states
        commands.SetViewport({.width = float(frame.width), .height = float(frame.height)});
        commands.SetScissor({.width = frame.width, .height = frame.height});

        const auto projection = camera.GetProjectionMatrix();
        const glm::mat4 skyInverseViewProjection = glm::inverse(projection * glm::mat4(glm::mat3(camera.GetViewMatrix())));
        const SkyParameters skyParameters{
            skyInverseViewProjection,
            glm::vec4(resources.sunDirection, m_Sun.GetAngularRadiusTangent()),
            glm::vec4(m_Sun.GetOrbitAxis(), 0),
        };
        commands.BindPipeline(m_SkyPipeline.Raw());
        commands.SetPushConstants(&skyParameters, sizeof(skyParameters));
        commands.Draw(3);

        commands.BindPipeline(m_WireframeEnabled ? m_WireframePipeline.Raw() : m_SolidPipeline.Raw());
        commands.UseResource(m_VoxelAtlas.Raw(), ResourceDeclarationScope::Pass);
        commands.UseResource(graph.GetBuffer(resources.sunBuffer), ResourceDeclarationScope::Pass);
        commands.UseResource(m_VoxelSampler.Raw());
        if (resources.voxelShadows) {
            commands.UseResource(graph.GetBuffer(resources.voxelVolume), ResourceDeclarationScope::Pass);
        }
        VoxelParameters params{
            .viewProjection = camera.GetViewProjectionMatrix(),
			.chunkOrigin = glm::vec4(0.0f),     // Will be updated per-chunk below.
            .cameraPositionFogStart = glm::vec4(camera.GetPosition(), Options::Rendering::FOG_START), 
            .fogEnd = Options::Rendering::FOG_END,
            .textureIndex = m_VoxelAtlas->GetSampledImageDescriptorIndex(),
            .samplerIndex = m_VoxelSampler->GetDescriptorIndex(),
			.vertexBufferIndex = 0,             // Will be updated per-chunk below.
            .sunDataIndex = graph.GetBuffer(resources.sunBuffer)->GetDescriptorIndex(),
            .voxelShadowIndex = resources.voxelShadows ? graph.GetBuffer(resources.voxelVolume)->GetDescriptorIndex() : 0,
        };

        for (const auto& [coord, visibleChunk] : resources.visibleChunks) {
            auto& chunk = *visibleChunk;
            params.chunkOrigin = glm::vec4(glm::vec3(coord) * float(Options::World::CHUNK_SIZE), 0.0f);
            params.vertexBufferIndex = chunk.vertices->GetDescriptorIndex();

            commands.SetPushConstants(&params, sizeof(params));
            commands.ClearDrawResourceDeclarations();
            commands.UseResource(chunk.vertices.Raw());

            commands.BindIndexBuffer(chunk.indices.Raw());
            commands.DrawIndexed(chunk.indexCount);
        }

        commands.EndRendering();
    });
}

bool Renderer::DrawFrame(const FrameContext& frame, const Camera& camera) {
    if (!UpdateChunks(camera.GetPosition()) || 
        !UpdateVoxelShadowVolume(camera.GetPosition())) {
        return false;
    }

    RenderGraph graph;
    FrameResources resources {
        .sunDirection = m_Sun.GetDirection(),
        .voxelShadows = bool(m_VoxelShadowVolume),
    };
    
    if (resources.voxelShadows) {
        resources.voxelVolume = graph.ImportBuffer(m_VoxelShadowVolume.Raw());
    }

    const ChunkCoord player = World::ChunkCoordinate(camera.GetPosition());
    CollectVisibleChunks(graph, resources, player, camera);
    AddSunDataPass(graph, resources);
    AddVoxelPass(graph, resources, frame, camera);

    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }

    return true;
}
