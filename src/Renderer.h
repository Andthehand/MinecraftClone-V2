#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>
#include <vector>

#include "Camera.h"
#include "World.h"

using namespace RealRHI;

class Renderer {
public:
	Renderer(Device& device, Swapchain& swapchain);
	~Renderer() = default;

	bool DrawFrame(const FrameContext& frameContext, const Camera& camera);
	void ToggleWireframe() { m_WireframeEnabled = !m_WireframeEnabled; }
	size_t GetLoadedChunkCount() const { return m_World.chunks.size(); }

private:
	bool CreateVoxelPipeline();
	bool CreateShadowPipeline();
	bool CreateSkyPipeline();

	bool UpdateChunks(glm::vec3 position);

    struct FrameResources;
    std::vector<RenderGraphUse> GetChunkResources(RenderGraph& graph, ChunkCoord player);
    void AddShadowDataPass(RenderGraph& graph, FrameResources& resources, const glm::mat4& lightViewProjection);
    void AddShadowPass(RenderGraph& graph, FrameResources& resources,
        const glm::mat4& lightViewProjection, ChunkCoord player);
    void AddVoxelPass(RenderGraph& graph, const FrameResources& resources,
        const FrameContext& frame, const Camera& camera, ChunkCoord player);
private:
	Device& m_Device;
	Swapchain& m_Swapchain;
	std::unique_ptr<TransientResourcePool> m_Pool;

	Ref<Pipeline> m_SolidPipeline;
	Ref<Pipeline> m_WireframePipeline;
    Ref<Pipeline> m_SkyPipeline;
    Ref<Pipeline> m_ShadowPipeline;

    Ref<Sampler> m_ShadowSampler;

	Ref<Texture> m_VoxelAtlas;
	Ref<Sampler> m_VoxelSampler;
	bool m_WireframeEnabled = false;

	World m_World;
};
