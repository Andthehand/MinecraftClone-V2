#include "Renderer.h"
#include "ChunkMesh.h"
#include "VoxelTextures.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <type_traits>
#include <glm/vec4.hpp>
#include <glm/matrix.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace {
	// Returns a list of chunk offsets to load, sorted by distance from the origin.
    const std::vector<ChunkCoord>& GetChunkLoadOffsets() {
        static const std::vector<ChunkCoord> offsets = [] {
            std::vector<ChunkCoord> result;
            constexpr int radius = Options::World::LOAD_RADIUS_CHUNKS;
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
    };

    struct alignas(16) SunData {
        glm::vec4 sunDirection;
        uint32_t shadowDataIndex;
		glm::vec3 padding; // std430 rounds the structure stride to 16-byte alignment.
    };
    static_assert(offsetof(SunData, shadowDataIndex) == 16);
    static_assert(sizeof(SunData) == 32); // Match the shader's std430 structure stride.

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
    if (!CreateVoxelPipeline() || !CreateSkyPipeline() || !CreateShadowPipeline()) {
        return;
    }

    m_Pool = std::make_unique<TransientResourcePool>(device, Options::Rendering::TRANSIENT_RESOURCE_POOL_BYTES);
}

bool Renderer::CreateVoxelPipeline() {
    Ref<Shader> voxelShader;
	const ShaderDesc shaderDesc{
		.moduleName = Options::Rendering::TERRAIN_SHADER_MODULE,
		.debugName = "VoxelRasterShader",
	};
    if (m_Device.CreateShader(shaderDesc, voxelShader) != Result::Success) {
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

bool Renderer::CreateShadowPipeline() {
    Ref<Shader> depthShader;
	constexpr ShaderDesc shaderDesc{
		.moduleName = Options::Rendering::SHADOW_SHADER_MODULE,
		.debugName = "ShadowDepthShader",
	};
	if (m_Device.CreateShader(shaderDesc, depthShader) != Result::Success) {
		std::cerr << "Failed to create shadow depth voxelShader.\n";
		return false;
	}

	const PipelineDesc pipelineDesc{
		.shader = depthShader.Raw(),
		.rasterState = { .cullMode = CullMode::Front },
		.depthState = { .depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less },
		.renderTargetFormats = { .depthFormat = TextureFormat::D32_Float },
		.debugName = "ShadowDepthPipeline",
	};
	if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_ShadowDepthPipeline) != Result::Success) {
		std::cerr << "Failed to create shadow depth pipeline.\n";
		return false;
	}

	constexpr SamplerDesc samplerDesc{
		.minFilter = Filter::Nearest, .magFilter = Filter::Nearest, .mipFilter = Filter::Nearest,
		.addressU = SamplerAddressMode::ClampToBorder, .addressV = SamplerAddressMode::ClampToBorder,
		.compareEnable = true, .compareOp = CompareOp::LessEqual, .borderColor = SamplerBorderColor::OpaqueWhite,
		.debugName = "ShadowComparisonSampler",
	};
	if (m_Device.CreateSampler(samplerDesc, m_ShadowComparisonSampler) != Result::Success) {
		std::cerr << "Failed to create shadow comparison sampler.\n";
		return false;
	}

	return true;
}

bool Renderer::CreateSkyPipeline() {
    Ref<Shader> skyShader;
    constexpr ShaderDesc shaderDesc{
		.moduleName = Options::Rendering::SKY_SHADER_MODULE,
		.debugName = "SkyShader",
	};
    if (m_Device.CreateShader(shaderDesc, skyShader) != Result::Success) {
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
        if (World::Distance(it->first, player) > Options::World::UNLOAD_RADIUS_CHUNKS) {
            // Render graph resource uses retain the buffers until GPU completion.
            it = m_World.chunks.erase(it);
        }
        else {
            ++it;
        }
    }

    uint32_t loaded = 0;
    for (const auto offset : GetChunkLoadOffsets()) {
        if (loaded == Options::World::MAX_CHUNKS_GENERATED_PER_FRAME) {
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
    struct ShadowVisibleChunk {
        ChunkCoord coord;
        World::Chunk* chunk;
        uint32_t cascadeMask; // Bit i marks visibility in cascade i.
    };
    std::vector<VisibleChunk> visibleChunks;
    std::vector<ShadowVisibleChunk> shadowVisibleChunks;
    std::vector<RenderGraphUse> geometryResourceUses;

    RenderGraphResource sunBuffer;
    glm::vec3 sunDirection;

    ShadowMaps::SceneData shadowMapData;
    RenderGraphResource shadowTexture;
	RenderGraphResource shadowDataBuffer;
    Ref<TextureView> sampledShadowView;
    std::array<Ref<TextureView>, 3> shadowDepthViews;
};

void Renderer::CollectVisibleChunks(RenderGraph& graph, FrameResources& resources, ChunkCoord player,
    const Camera& camera) {
    const auto planes = camera.GetFrustumPlanes();

	// Since we are doing 3 cascaded shadow maps, we need to check visibility against the light frustum as well.
    std::array<std::array<glm::vec4, 6>, 3> lightPlanes;
    for (uint32_t cascade = 0; cascade < lightPlanes.size(); ++cascade) {
        lightPlanes[cascade] = ShadowMaps::MatrixPlanes(resources.shadowMapData.lightMatrices[cascade]);
    }

    auto& uses = resources.geometryResourceUses;
    for (auto& [coord, chunk] : m_World.chunks) {
        if (chunk.indexCount == 0 || chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) {
            continue;
        }

        const glm::vec3 chunkMinimum = glm::vec3(coord) * float(Options::World::CHUNK_SIZE);
        const auto chunkMaximum = chunkMinimum + glm::vec3(float(Options::World::CHUNK_SIZE));

        const bool cameraVisible = Camera::IsBoxVisible(planes, chunkMinimum, chunkMaximum);
        
        uint32_t cascadeMask = 0;
        for (uint32_t cascade = 0; cascade < lightPlanes.size(); ++cascade) {
            if (Camera::IsBoxVisible(lightPlanes[cascade], chunkMinimum, chunkMaximum)) {
                cascadeMask |= 1u << cascade;
            }
        }

        if (!cameraVisible && cascadeMask == 0) {
			// Not visible to camera or any shadow cascade, skip...
            continue;
        }

        if (cameraVisible) {
            resources.visibleChunks.push_back({ coord, &chunk });
        }
        if (cascadeMask != 0) {
            resources.shadowVisibleChunks.push_back({ coord, &chunk, cascadeMask });
        }

        uses.push_back({graph.ImportBuffer(chunk.vertices.Raw()),
            {PipelineStage::VertexShader, ResourceAccess::ShaderRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
        uses.push_back({graph.ImportBuffer(chunk.indices.Raw()),
            {PipelineStage::VertexInput, ResourceAccess::IndexRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
    }

    // Near chunks populate depth first, reducing shading of hidden surfaces.
    const glm::vec3 cameraPosition = camera.GetPosition();
    const auto distanceSquared = [cameraPosition](ChunkCoord coord) {
        const glm::vec3 center = (glm::vec3(coord) + glm::vec3(0.5f)) * float(Options::World::CHUNK_SIZE);
        const glm::vec3 delta = center - cameraPosition;
        return glm::dot(delta, delta);
    };
    std::sort(resources.visibleChunks.begin(), resources.visibleChunks.end(),
        [&distanceSquared](const auto& a, const auto& b) {
            return distanceSquared(a.coord) < distanceSquared(b.coord);
        }
    );
}

void Renderer::BufferDataInitPass(RenderGraph& graph, FrameResources& resources) {
    const SunData sunData{.sunDirection = glm::vec4(resources.sunDirection, 0)};
    resources.sunBuffer = graph.CreateBuffer({
        .size = sizeof(SunData), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .debugName = "SunLightingData",
    });

	constexpr ResourceState sunDataWrite{ PipelineStage::AllCommands, ResourceAccess::MemoryWrite };    
    graph.AddPass({
        .name = "InitializeBufferData",
        .resources = {{resources.sunBuffer, sunDataWrite, TextureLayout::Undefined, RenderGraphAccess::Write},
    }}, [&graph, &resources, sunData](CommandList& commands) {
        auto* buffer = graph.GetBuffer(resources.sunBuffer);
        commands.UseResource(buffer);

        auto data = sunData;
        data.shadowDataIndex = graph.GetBuffer(resources.shadowDataBuffer)->GetDescriptorIndex();
        if (buffer->WriteData(&data, sizeof(data)) != Result::Success) {
            std::cerr << "Failed to write sun lighting data.\n";
        }
    });
}

void Renderer::AddShadowMapPasses(RenderGraph& graph, FrameResources& resources) {
    constexpr uint32_t layers = 3;
    constexpr uint32_t resolution = Options::Rendering::SHADOW_MAP_RESOLUTION;

    resources.shadowTexture = graph.CreateTexture({
        .width = resolution, .height = resolution, .arrayLayers = layers,
        .format = TextureFormat::D32_Float, .usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource,
        .debugName = "DirectionalShadowMaps"
    });
    resources.shadowDataBuffer = graph.CreateBuffer({
        .size = sizeof(ShadowMaps::SceneData), 
        .usage = BufferUsage::Storage, .memoryUsage = MemoryUsage::CPUToGPU, 
        .debugName = "DirectionalShadowData"
    });

	constexpr ResourceState shadowDataWrite{ PipelineStage::AllCommands, ResourceAccess::MemoryWrite };
    graph.AddPass({.name = "InitializeShadowMaps", 
        .resources = {{resources.shadowDataBuffer, shadowDataWrite, TextureLayout::Undefined, RenderGraphAccess::Write}}},
        [this, &graph, &resources, layers](CommandList&) {
            auto* texture = graph.GetTexture(resources.shadowTexture);
            if (m_Device.CreateTextureView(texture, { .type = TextureViewType::View2DArray, .arrayLayerCount = layers,
                .usage = TextureUsage::ShaderResource }, resources.sampledShadowView) != Result::Success) {
                std::cerr << "Could not create sampled shadow view.\n";
                return;
            }
            
            for (uint32_t i = 0; i < layers; ++i) {
                if (m_Device.CreateTextureView(texture, { .baseArrayLayer = i, .usage = TextureUsage::DepthStencil }, resources.shadowDepthViews[i]) != Result::Success) {
                    std::cerr << "Could not create shadow depth view.\n";
                    return;
                }
            }

            auto data = resources.shadowMapData;
            data.indices = {resources.sampledShadowView->GetSampledImageDescriptorIndex(), m_ShadowComparisonSampler->GetDescriptorIndex(), 0u, 0u};
            if (graph.GetBuffer(resources.shadowDataBuffer)->WriteData(&data, sizeof(data)) != Result::Success) {
                std::cerr << "Could not upload shadow parameters.\n";
            }
        });

    auto uses = resources.geometryResourceUses;
	constexpr ResourceState shadowTextureWrite{ PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
		                                        ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite };
	constexpr ResourceState shadowDataRead{ PipelineStage::VertexShader, ResourceAccess::ShaderRead };
    uses.push_back({resources.shadowTexture, shadowTextureWrite, TextureLayout::DepthStencilAttachment, RenderGraphAccess::Write});
    uses.push_back({resources.shadowDataBuffer, shadowDataRead, TextureLayout::Undefined, RenderGraphAccess::Read});
    graph.AddPass({.name = "RenderDirectionalShadowMaps", .resources = std::move(uses)},
        [this, &resources, layers](CommandList& commands) {
            for (uint32_t cascade = 0; cascade < layers; ++cascade) {
				commands.BeginDebugLabel(("ShadowMapCascade: " + std::to_string(cascade)).c_str());
                commands.BeginRendering({
                    .hasDepth = true, .depthAttachment = {.target = resources.shadowDepthViews[cascade].Raw()},
                    .renderArea = {.width = resolution, .height = resolution}
                });

				// Set dynamic states
                commands.SetViewport({.width = float(resolution), .height = float(resolution)});
                commands.SetScissor({.width = resolution, .height = resolution});

                commands.BindPipeline(m_ShadowDepthPipeline.Raw());
                for (const auto& caster : resources.shadowVisibleChunks) {
                    if ((caster.cascadeMask & (1u << cascade)) == 0) continue;
                    const glm::vec3 minimum = glm::vec3(caster.coord) * float(Options::World::CHUNK_SIZE);
                    struct DepthParameters { glm::mat4 viewProjection; glm::vec4 origin; uint32_t vertices; };
                    const DepthParameters push{resources.shadowMapData.lightMatrices[cascade], glm::vec4(minimum, 0), caster.chunk->vertices->GetDescriptorIndex()};
                    
                    commands.ClearDrawResourceDeclarations(); commands.UseResource(caster.chunk->vertices.Raw());
                    commands.SetPushConstants(&push, sizeof(push)); commands.BindIndexBuffer(caster.chunk->indices.Raw());
                    commands.DrawIndexed(caster.chunk->indexCount);
                }

                commands.EndRendering();
                commands.EndDebugLabel();
            }
        });
}

void Renderer::AddVoxelPass(RenderGraph& graph, FrameResources& resources,
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
    auto uses = resources.geometryResourceUses;
    uses.push_back({backbuffer, backbufferWrite, TextureLayout::ColorAttachment, RenderGraphAccess::Write});
    uses.push_back({depth, depthWrite, TextureLayout::DepthStencilAttachment, RenderGraphAccess::Write});
    uses.push_back({resources.sunBuffer, sampled, TextureLayout::Undefined, RenderGraphAccess::Read});
    uses.push_back({resources.shadowTexture, sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});
    uses.push_back({resources.shadowDataBuffer, sampled, TextureLayout::Undefined, RenderGraphAccess::Read});
    uses.push_back({graph.ImportTexture(m_VoxelAtlas.Raw(), TextureLayout::ShaderRead, sampled),
        sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});

    graph.AddPass({.name = "RasterizeVoxels.CSM3", .resources = std::move(uses)},
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
        commands.UseResource(resources.sampledShadowView.Raw(), ResourceDeclarationScope::Pass);
        commands.UseResource(graph.GetBuffer(resources.shadowDataBuffer), ResourceDeclarationScope::Pass);
        commands.UseResource(m_ShadowComparisonSampler.Raw());
        VoxelParameters params{
            .viewProjection = camera.GetViewProjectionMatrix(),
			.chunkOrigin = glm::vec4(0.0f),     // Will be updated per-chunk below.
            .cameraPositionFogStart = glm::vec4(camera.GetPosition(), Options::Rendering::FOG_START_DISTANCE),
            .fogEnd = Options::Rendering::FOG_END_DISTANCE,
            .textureIndex = m_VoxelAtlas->GetSampledImageDescriptorIndex(),
            .samplerIndex = m_VoxelSampler->GetDescriptorIndex(),
			.vertexBufferIndex = 0,             // Will be updated per-chunk below.
            .sunDataIndex = graph.GetBuffer(resources.sunBuffer)->GetDescriptorIndex(),
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
    if (!UpdateChunks(camera.GetPosition())) {
        return false;
    }

    RenderGraph graph;
    FrameResources resources {
        .sunDirection = m_Sun.GetDirection(),
    };
    
    const ChunkCoord player = World::ChunkCoordinate(camera.GetPosition());
    resources.shadowMapData = ShadowMaps::BuildData(camera, resources.sunDirection, frame.width, frame.height);
    CollectVisibleChunks(graph, resources, player, camera);
    AddShadowMapPasses(graph, resources);
    BufferDataInitPass(graph, resources);
    AddVoxelPass(graph, resources, frame, camera);

    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }

    return true;
}
