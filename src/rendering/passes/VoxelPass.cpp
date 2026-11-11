#include "rendering/FrameResources.h"
#include "rendering/ShaderData.h"
#include "diagnostics/Profiling.h"
#include <glm/matrix.hpp>
#include <iostream>
#include <string>

using namespace RealRHI;
using namespace ShaderData;

void Renderer::AddVoxelPass(RenderGraph& graph, FrameResources& resources,
    const FrameContext& frame, const Camera& camera) {
    MC_PROFILE_ZONE("Build voxel pass");
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
        commands.BindShaderProgram(m_SkyShader.Raw());
        commands.SetGraphicsState({
            .raster = {.cullMode = CullMode::None},
            .depth = {.depthTestEnable = false, .depthWriteEnable = false},
        });
        commands.SetPushConstants(&skyParameters, sizeof(skyParameters));
        commands.Draw(3);

        commands.BindShaderProgram(m_VoxelShader.Raw());
        commands.SetGraphicsState({
            .raster = {.cullMode = CullMode::Front,
                .fillMode = m_WireframeEnabled ? FillMode::Wireframe : FillMode::Solid},
            .depth = {.depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less},
        });

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
            // Clearing declarations also clears shared shader resources; restore them for each draw.
            commands.ClearResourceDeclarations();
            commands.UseResource(m_VoxelAtlas.Raw());
            commands.UseResource(graph.GetBuffer(resources.sunBuffer));
            commands.UseResource(m_VoxelSampler.Raw());
            commands.UseResource(resources.sampledShadowView.Raw());
            commands.UseResource(graph.GetBuffer(resources.shadowDataBuffer));
            commands.UseResource(m_ShadowComparisonSampler.Raw());
            commands.UseResource(chunk.vertices.Raw());

            commands.BindIndexBuffer(chunk.indices.Raw());
            commands.DrawIndexed(chunk.indexCount);
        }

        commands.EndRendering();
    });
}

