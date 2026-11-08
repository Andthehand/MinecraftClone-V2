#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>
#include <vector>

#include "Camera.h"
#include "Sun.h"
#include "World.h"
#include "ShadowMaps.h"

using namespace RealRHI;

class Renderer {
public:
	Renderer(Device& device, Swapchain& swapchain);
	~Renderer() = default;

	bool DrawFrame(const FrameContext& frameContext, const Camera& camera);
	void ToggleWireframe() { m_WireframeEnabled = !m_WireframeEnabled; }

private:
	bool CreateVoxelPipeline();
	bool CreateSkyPipeline();
    bool CreateShadowPipeline();

	bool UpdateChunks(glm::vec3 position);

    struct FrameResources;
    void CollectVisibleChunks(RenderGraph& graph, FrameResources& resources, ChunkCoord player,
        const Camera& camera);
    void BufferDataInitPass(RenderGraph& graph, FrameResources& resources);
    void AddShadowMapPasses(RenderGraph& graph, FrameResources& resources);
    void AddVoxelPass(RenderGraph& graph, FrameResources& resources,
        const FrameContext& frame, const Camera& camera);
private:
	Device& m_Device;
	Swapchain& m_Swapchain;
	std::unique_ptr<TransientResourcePool> m_Pool;

	Ref<Pipeline> m_SolidPipeline;
	Ref<Pipeline> m_WireframePipeline;
    Ref<Pipeline> m_SkyPipeline;
	Ref<Pipeline> m_ShadowDepthPipeline;

	Ref<Texture> m_VoxelAtlas;
	Ref<Sampler> m_VoxelSampler;
	Ref<Sampler> m_ShadowComparisonSampler;
	bool m_WireframeEnabled = false;
    Sun m_Sun;

	World m_World;
};
