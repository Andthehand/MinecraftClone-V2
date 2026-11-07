#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>
#include <vector>

#include "Camera.h"
#include "Sun.h"
#include "World.h"

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

	bool UpdateChunks(glm::vec3 position);
    bool UpdateVoxelShadowVolume(glm::vec3 position);

    struct FrameResources;
    void CollectVisibleChunks(RenderGraph& graph, FrameResources& resources, ChunkCoord player,
        const Camera& camera);
    void AddSunDataPass(RenderGraph& graph, FrameResources& resources);
    void AddVoxelPass(RenderGraph& graph, const FrameResources& resources,
        const FrameContext& frame, const Camera& camera);
private:
	Device& m_Device;
	Swapchain& m_Swapchain;
	std::unique_ptr<TransientResourcePool> m_Pool;

	Ref<Pipeline> m_SolidPipeline;
	Ref<Pipeline> m_WireframePipeline;
    Ref<Pipeline> m_SkyPipeline;

	Ref<Texture> m_VoxelAtlas;
	Ref<Sampler> m_VoxelSampler;
	bool m_WireframeEnabled = false;
    Ref<Buffer> m_VoxelShadowVolume;
    ChunkCoord m_VoxelShadowCenter{};
    Sun m_Sun;

	World m_World;
};
