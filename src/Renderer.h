#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>

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
	bool UpdateChunks(Vec3 position);
	Device& m_Device;
	Swapchain& m_Swapchain;
	Ref<Pipeline> m_SolidPipeline;
	Ref<Pipeline> m_WireframePipeline;
	bool m_WireframeEnabled = false;
	std::unique_ptr<TransientResourcePool> m_Pool;

	World m_World;
};
