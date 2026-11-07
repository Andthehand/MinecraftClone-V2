#include "Renderer.h"
#include "ChunkMesh.h"
#include "VoxelTextures.h"
#include "ShadowCamera.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <glm/vec4.hpp>
#include <glm/trigonometric.hpp>
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
        uint32_t shadowDataIndex;
        uint32_t shadowTextureIndex;
        uint32_t shadowSamplerIndex;
    };

    struct ShadowParameters {
        glm::mat4 viewProjection;
        glm::vec4 chunkOrigin;
        uint32_t vertexBufferIndex;
    };

    struct ShadowData {
        glm::mat4 viewProjection;
        glm::vec4 sunDirectionRadius;
        glm::vec4 texelBiasFade; // Texel size, comparison bias, fade start, normal offset.
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
    if (!CreateVoxelPipeline() || !CreateShadowPipeline() || !CreateSkyPipeline()) {
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

bool Renderer::CreateShadowPipeline() {
    Ref<Shader> shadowShader;
    if (m_Device.CreateShader({ .moduleName = Options::Rendering::SHADOW_SHADER_MODULE,
                            .debugName = "SunShadowShader" }, shadowShader) != Result::Success) {
        std::cerr << "Failed to create sun shadow shader.\n";
        return false;
    }

	PipelineDesc pipelineDesc{
		.shader = shadowShader.Raw(),
		.rasterState = { .cullMode = CullMode::None,
		    // Depth bias is required to avoid self-shadowing artifacts on the terrain.
			.depthBias = { .enable = true, .constantFactor = 1.25f, .slopeFactor = 1.75f } 
        },
		.renderTargetFormats = { .depthFormat = TextureFormat::D32_Float },
		.debugName = "SunShadowPipeline",
	};
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_ShadowPipeline) != Result::Success) {
        std::cerr << "Failed to create sun shadow pipeline.\n";
        return false;
    }

	SamplerDesc samplerDesc{
		.minFilter = Filter::Linear, .magFilter = Filter::Linear, .mipFilter = Filter::Nearest,
		.addressU = SamplerAddressMode::ClampToBorder, .addressV = SamplerAddressMode::ClampToBorder,
		.addressW = SamplerAddressMode::ClampToBorder,
		.compareEnable = true, .compareOp = CompareOp::LessEqual,
		.borderColor = SamplerBorderColor::OpaqueWhite, .debugName = "SunShadowSampler",
	};
    if (m_Device.CreateSampler(samplerDesc, m_ShadowSampler) != Result::Success) {
        std::cerr << "Failed to create sun shadow sampler.\n";
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
    RenderGraphResource shadowBuffer;
    RenderGraphResource shadow;
    std::vector<RenderGraphUse> geometry;
    glm::vec3 sunDirection;
};

glm::vec3 Renderer::GetSunDirection() const {
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_SunStartTime).count();
    const float angle = glm::radians(float(360.0 * std::fmod(elapsed, Options::Rendering::SUN_ORBIT_PERIOD_SECONDS)
        / Options::Rendering::SUN_ORBIT_PERIOD_SECONDS));
    const glm::vec3 initial = glm::normalize(Options::Rendering::SUN_DIRECTION);
    return glm::normalize(glm::vec3(glm::rotate(glm::mat4(1), angle, SunOrbitAxis()) * glm::vec4(initial, 0)));
}

std::vector<RenderGraphUse> Renderer::GetChunkResources(RenderGraph& graph, ChunkCoord player) {
    std::vector<RenderGraphUse> uses;
    for (auto& [coord, chunk] : m_World.chunks) {
        if (chunk.indexCount == 0 || chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) {
			// Not ready to mesh yet, skip...
            continue;
        }

        uses.push_back({graph.ImportBuffer(chunk.vertices.Raw()),
            {PipelineStage::VertexShader, ResourceAccess::ShaderRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
        uses.push_back({graph.ImportBuffer(chunk.indices.Raw()),
            {PipelineStage::VertexInput, ResourceAccess::IndexRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
    }

    return uses;
}

void Renderer::AddShadowDataPass(RenderGraph& graph, FrameResources& resources, const glm::mat4& lightViewProjection) {
    const ShadowData shadowData{
        .viewProjection = lightViewProjection,
        .sunDirectionRadius = glm::vec4(resources.sunDirection, Options::Rendering::SHADOW_RADIUS),
        .texelBiasFade = {1.0f / Options::Rendering::SHADOW_RESOLUTION, 0.00001f, Options::Rendering::SHADOW_RADIUS * 0.8f, 0.04f},
    };
    resources.shadowBuffer = graph.CreateBuffer({
        .size = sizeof(ShadowData), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .debugName = "SunShadowData",
    });

    graph.AddPass({
        .name = "InitializeSunShadowData",
        .resources = {{resources.shadowBuffer, {PipelineStage::AllCommands, ResourceAccess::MemoryWrite},
                        TextureLayout::Undefined, RenderGraphAccess::Write},
    }}, [&graph, &resources, shadowData](CommandList& commands) {
        auto* buffer = graph.GetBuffer(resources.shadowBuffer);
        commands.UseResource(buffer);

        if (buffer->WriteData(&shadowData, sizeof(shadowData)) != Result::Success) {
            std::cerr << "Failed to write sun shadow data.\n";
        }
    });
}

void Renderer::AddShadowPass(RenderGraph& graph, FrameResources& resources,
    const glm::mat4& lightViewProjection, ChunkCoord player) {
    resources.shadow = graph.CreateTexture({
        .width = Options::Rendering::SHADOW_RESOLUTION, 
        .height = Options::Rendering::SHADOW_RESOLUTION,
        .format = TextureFormat::D32_Float, 
        .usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource,
        .debugName = "SunShadowDepth",
    });

    constexpr ResourceState depthWrite{
        PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
        ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite,
    };
    std::vector<RenderGraphUse> uses = resources.geometry;
    uses.push_back({resources.shadow, depthWrite, TextureLayout::DepthStencilAttachment, RenderGraphAccess::Write});
    graph.AddPass({.name = "SunShadows", .resources = std::move(uses)},
        [this, &graph, &resources, lightViewProjection, player](CommandList& commands) {
            constexpr auto resolution = Options::Rendering::SHADOW_RESOLUTION;
            commands.BeginRendering({.hasDepth = true,
                .depthAttachment = {.target = graph.GetTexture(resources.shadow)->GetTextureView()},
                .renderArea = {.width = resolution, .height = resolution},
            });

			// Dynamic states
            commands.SetViewport({.width = float(resolution), .height = float(resolution)});
            commands.SetScissor({.width = resolution, .height = resolution});
            commands.BindPipeline(m_ShadowPipeline.Raw());

            ShadowParameters params{
                .viewProjection = lightViewProjection, 
                .chunkOrigin = glm::vec4(0),    // Will be updated per-chunk below.
                .vertexBufferIndex = 0          // Will be updated per-chunk below.
            };    
            // Off-camera geometry can cast visible shadows.
            for (auto& [coord, chunk] : m_World.chunks) {
                if (chunk.indexCount == 0 || chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) continue;
                params.chunkOrigin = glm::vec4(glm::vec3(coord) * float(Options::World::CHUNK_SIZE), 0);
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

void Renderer::AddVoxelPass(RenderGraph& graph, const FrameResources& resources,
    const FrameContext& frame, const Camera& camera, ChunkCoord player) {
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
    uses.push_back({resources.shadow, sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});
    uses.push_back({resources.shadowBuffer, sampled, TextureLayout::Undefined, RenderGraphAccess::Read});
    uses.push_back({graph.ImportTexture(m_VoxelAtlas.Raw(), TextureLayout::ShaderRead, sampled),
        sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});

    graph.AddPass({.name = "RasterizeVoxels", .resources = std::move(uses)},
        [this, &graph, &resources, frame, camera, player, depth](CommandList& commands) {
            commands.BeginRendering({
                .colorAttachments = {{.target = frame.backBufferView, .clearColor = {0.45f, 0.65f, 0.9f, 1.0f}}},
                .hasDepth = true,
                .depthAttachment = {.target = graph.GetTexture(depth)->GetTextureView()},
                .renderArea = {.width = frame.width, .height = frame.height},
        });

        // Set dynamic states
        commands.SetViewport({.width = float(frame.width), .height = float(frame.height)});
        commands.SetScissor({.width = frame.width, .height = frame.height});

        const auto projection = camera.GetProjectionMatrix(float(frame.width) / float(frame.height));
        const glm::mat4 skyInverseViewProjection = glm::inverse(projection * glm::mat4(glm::mat3(camera.GetViewMatrix())));
        const SkyParameters skyParameters{
            skyInverseViewProjection,
            glm::vec4(resources.sunDirection,
                glm::tan(glm::radians(Options::Rendering::SUN_ANGULAR_SIZE_DEGREES * 0.5f))),
            glm::vec4(SunOrbitAxis(), 0),
        };
        commands.BindPipeline(m_SkyPipeline.Raw());
        commands.SetPushConstants(&skyParameters, sizeof(skyParameters));
        commands.Draw(3);

        commands.BindPipeline(m_WireframeEnabled ? m_WireframePipeline.Raw() : m_SolidPipeline.Raw());
        VoxelParameters params{
            .viewProjection = projection * camera.GetViewMatrix(), 
			.chunkOrigin = glm::vec4(0.0f),     // Will be updated per-chunk below.
            .cameraPositionFogStart = glm::vec4(camera.GetPosition(), Options::Rendering::FOG_START), 
            .fogEnd = Options::Rendering::FOG_END,
            .textureIndex = m_VoxelAtlas->GetSampledImageDescriptorIndex(),
            .samplerIndex = m_VoxelSampler->GetDescriptorIndex(),
			.vertexBufferIndex = 0,             // Will be updated per-chunk below.
            .shadowDataIndex = graph.GetBuffer(resources.shadowBuffer)->GetDescriptorIndex(),
            .shadowTextureIndex = graph.GetTexture(resources.shadow)->GetSampledImageDescriptorIndex(),
            .shadowSamplerIndex = m_ShadowSampler->GetDescriptorIndex(),
        };

        for (auto& [coord, chunk] : m_World.chunks) {
            if (chunk.indexCount == 0 || chunk.meshDirty || !m_World.CanMeshChunk(coord, player)) {
				// Not ready to render yet, skip...
                continue;
            }

            params.chunkOrigin = glm::vec4(glm::vec3(coord) * float(Options::World::CHUNK_SIZE), 0.0f);
            params.vertexBufferIndex = chunk.vertices->GetDescriptorIndex();
            commands.SetPushConstants(&params, sizeof(params));
            commands.ClearDrawResourceDeclarations();
            commands.UseResource(chunk.vertices.Raw());

            commands.BindIndexBuffer(chunk.indices.Raw());
            commands.DrawIndexed(chunk.indexCount);
        }

        commands.UseResource(m_VoxelAtlas.Raw(), ResourceDeclarationScope::Pass);
        commands.UseResource(graph.GetBuffer(resources.shadowBuffer), ResourceDeclarationScope::Pass);
        commands.UseResource(graph.GetTexture(resources.shadow), ResourceDeclarationScope::Pass);
        commands.UseResource(m_VoxelSampler.Raw());
        commands.UseResource(m_ShadowSampler.Raw());

        commands.EndRendering();
    });
}

bool Renderer::DrawFrame(const FrameContext& frame, const Camera& camera) {
    if (!UpdateChunks(camera.GetPosition())) {
        return false;
    }

    RenderGraph graph;
    FrameResources resources;
    const ChunkCoord player = World::ChunkCoordinate(camera.GetPosition());
    resources.sunDirection = GetSunDirection();
    const glm::mat4 lightViewProjection = ShadowViewProjection(camera.GetPosition(), resources.sunDirection);
    resources.geometry = GetChunkResources(graph, player);
    AddShadowDataPass(graph, resources, lightViewProjection);
    AddShadowPass(graph, resources, lightViewProjection, player);
    AddVoxelPass(graph, resources, frame, camera, player);

    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }

    return true;
}
