#include "rendering/FrameResources.h"
#include "rendering/ShaderData.h"
#include "diagnostics/Profiling.h"
#include <glm/matrix.hpp>
#include <iostream>
#include <string>

using namespace RealRHI;
using namespace ShaderData;

void Renderer::AddShadowMapPasses(RenderGraph& graph, FrameResources& resources) {
    MC_PROFILE_ZONE("Build shadow passes");
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
                    const DepthParameters push{resources.shadowMapData.lightMatrices[cascade], glm::vec4(minimum, 0), caster.chunk->vertices->GetDescriptorIndex()};
                    
                    commands.ClearResourceDeclarations(); commands.UseResource(caster.chunk->vertices.Raw());
                    commands.SetPushConstants(&push, sizeof(push)); commands.BindIndexBuffer(caster.chunk->indices.Raw());
                    commands.DrawIndexed(caster.chunk->indexCount);
                }

                commands.EndRendering();
                commands.EndDebugLabel();
            }
        });
}

