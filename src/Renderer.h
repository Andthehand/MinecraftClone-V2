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
	size_t GetLoadedChunkCount() const { return m_World.chunks.size(); }
	bool IsChunkLoaded(ChunkCoord coord) const { return m_World.chunks.contains(coord); }

private:
	bool UpdateChunks(Vec3 position);
	Device& m_Device;
	Swapchain& m_Swapchain;
	Ref<Pipeline> m_ComputeRaytracePipeline;
	std::array<uint32_t, 3> m_RaytraceWorkgroupSize{1, 1, 1};
	Ref<Buffer> m_ChunkBuffer;
	Ref<Buffer> m_ChunkTable;
	std::unique_ptr<TransientResourcePool> m_Pool;

	World m_World;
	ChunkCoord m_GridOrigin{};
	FreeListAllocator m_NodeAllocator;

	// Used for deferred freeing of GPU allocations
	struct RetiredAllocation {
		FreeListAllocator::Allocation allocation;
		RenderGraphSubmission lastUse;
	};
	std::vector<RetiredAllocation> m_RetiredAllocations;	
	RenderGraphSubmission m_LastSubmission;
};
