#include "Renderer.h"
#include "ChunkMesh.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/vec4.hpp>
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

    struct Parameters {
        glm::mat4 viewProjection;
        glm::vec4 chunkOrigin;
        uint32_t textureIndex;
        uint32_t samplerIndex;
        uint32_t vertexBufferIndex;
    };
    static_assert(std::is_standard_layout_v<Parameters>);
    static_assert(sizeof(Parameters) == 92);
    static_assert(offsetof(Parameters, viewProjection) == 0);
    static_assert(offsetof(Parameters, chunkOrigin) == 64);
    static_assert(offsetof(Parameters, textureIndex) == 80);
    static_assert(offsetof(Parameters, samplerIndex) == 84);
    static_assert(offsetof(Parameters, vertexBufferIndex) == 88);
}

Renderer::Renderer(Device& device, Swapchain& swapchain)
    : m_Device(device), m_Swapchain(swapchain) {
    const auto pixels = VoxelTextures::BuildAtlas();
    if (device.CreateTexture({
            .width = VoxelTextures::WIDTH, .height = VoxelTextures::HEIGHT,
            .format = TextureFormat::RGBA8_UNorm,
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
    Ref<Shader> shader;
    if (device.CreateShader({.moduleName = Options::Rendering::SHADER_MODULE,
                            .debugName = "VoxelRasterShader"}, shader) != Result::Success) {
        std::cerr << "Failed to create voxel raster shader.\n";
        return;
    }

    PipelineDesc pipelineDesc{
        .shader = shader.Raw(),
        .rasterState = {.cullMode = CullMode::None, .fillMode = FillMode::Solid},
        .depthState = {.depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less},
        .renderTargetFormats = {.colorFormats = {swapchain.GetBackBufferFormat()}, .depthFormat = TextureFormat::D32_Float},
        .debugName = "VoxelSolidPipeline",
    };
    if (device.CreateGraphicsPipeline(pipelineDesc, m_SolidPipeline) != Result::Success) {
        std::cerr << "Failed to create solid voxel pipeline.\n";
        return;
    }
    pipelineDesc.rasterState.fillMode = FillMode::Wireframe;
    pipelineDesc.debugName = "VoxelWireframePipeline";
    if (device.CreateGraphicsPipeline(pipelineDesc, m_WireframePipeline) != Result::Success) {
        std::cerr << "Failed to create wireframe voxel pipeline.\n";
        return;
    }
    m_Pool = std::make_unique<TransientResourcePool>(device, Options::Rendering::TRANSIENT_POOL_BYTES);
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
        const ChunkMesh mesh = BuildChunkMesh(chunk.octree);
        if (!mesh.indices.empty()) {
            if (m_Device.CreateBuffer({
                    .size = mesh.vertices.size() * sizeof(VoxelVertex),
                    .usage = BufferUsage::Storage,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.vertices.data(), .debugName = "ChunkVertices",
                }, chunk.vertices) != Result::Success ||
                m_Device.CreateBuffer({
                    .size = mesh.indices.size() * sizeof(uint32_t),
                    .usage = BufferUsage::Index,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.indices.data(), .debugName = "ChunkIndices",
                }, chunk.indices) != Result::Success) {
                m_World.chunks.erase(it);
                std::cerr << "Failed to upload chunk mesh.\n";

                return false;
            }
            chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());
        }
        loaded++;
    }

    return true;
}

bool Renderer::DrawFrame(const FrameContext& frame, const Camera& camera) {
    if (!m_SolidPipeline || !m_WireframePipeline || !m_VoxelAtlas || !m_VoxelSampler ||
        !m_Pool || !UpdateChunks(camera.GetPosition())) return false;
    RenderGraph graph;
    const auto backbuffer = graph.ImportBackBuffer(m_Swapchain, frame);
    const auto depth = graph.CreateTexture({
        .width = frame.width, .height = frame.height,
        .format = TextureFormat::D32_Float, .usage = TextureUsage::DepthStencil,
        .debugName = "VoxelDepth",
    });

	constexpr ResourceState backbufferWrite{ PipelineStage::ColorAttachmentOutput, ResourceAccess::ColorAttachmentWrite };
	constexpr ResourceState depthWrite{
        PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
		ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite
    };
    std::vector<RenderGraphUse> uses{
        {backbuffer, backbufferWrite, TextureLayout::ColorAttachment, RenderGraphAccess::Write},
        {depth, depthWrite, TextureLayout::DepthStencilAttachment, RenderGraphAccess::Write},
    };
    constexpr ResourceState sampled{PipelineStage::FragmentShader, ResourceAccess::ShaderRead};
    uses.push_back({graph.ImportTexture(m_VoxelAtlas.Raw(), TextureLayout::ShaderRead, sampled),
        sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read});
    for (auto& [coord, chunk] : m_World.chunks) {
        if (chunk.indexCount == 0) {
			// No geometry to render, skip...
            continue;
        }

        uses.push_back({graph.ImportBuffer(chunk.vertices.Raw()),
            {PipelineStage::VertexShader, ResourceAccess::ShaderRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
        uses.push_back({graph.ImportBuffer(chunk.indices.Raw()),
            {PipelineStage::VertexInput, ResourceAccess::IndexRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
    }
    graph.AddPass({.name = "RasterizeVoxels", .resources = std::move(uses)}, [&](CommandList& commands) {
        commands.BeginRendering({
            .colorAttachments = {{.target = frame.backBufferView, .clearColor = {0.45f, 0.65f, 0.9f, 1.0f}}},
            .hasDepth = true,
            .depthAttachment = {.target = graph.GetTexture(depth)->GetTextureView()},
            .renderArea = {.width = frame.width, .height = frame.height},
        });

        // Set dynamic states
        commands.SetViewport({.width = float(frame.width), .height = float(frame.height)});
        commands.SetScissor({.width = frame.width, .height = frame.height});
        commands.BindPipeline(m_WireframeEnabled ? m_WireframePipeline.Raw() : m_SolidPipeline.Raw());

        // Push constants
        auto projection = glm::perspectiveRH_ZO(camera.GetFovYRadians(), float(frame.width) / float(frame.height),
            Options::Rendering::NEAR_PLANE, Options::Rendering::FAR_PLANE);
        // Vulkan uses [0,1] depth and a positive-height viewport, so flip clip-space Y.
        projection[1][1] *= -1.0f;
        Parameters params{
            projection * camera.GetViewMatrix(), glm::vec4(0.0f),
            m_VoxelAtlas->GetSampledImageDescriptorIndex(), m_VoxelSampler->GetDescriptorIndex(),
            0,
        };

		// Draw all loaded chunks
        for (auto& [coord, chunk] : m_World.chunks) {
            if (chunk.indexCount == 0) continue;
            params.chunkOrigin = glm::vec4(glm::vec3(coord) * float(Options::World::CHUNK_SIZE), 0.0f);
            params.vertexBufferIndex = chunk.vertices->GetDescriptorIndex();
            commands.SetPushConstants(&params, sizeof(params));
            // Descriptor indices alone do not declare shader access or retain resources.
            commands.ClearResourceDeclarations();
            commands.UseResource(chunk.vertices.Raw());
            commands.UseResource(m_VoxelAtlas.Raw());
            commands.UseResource(m_VoxelSampler.Raw());
            commands.BindIndexBuffer(chunk.indices.Raw());
            commands.DrawIndexed(chunk.indexCount);
        }

        commands.EndRendering();
    });
    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }
    return true;
}
