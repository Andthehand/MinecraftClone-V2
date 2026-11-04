#include <Device.h>
#include <RenderGraph/TransientResourcePool.h>

#include "Camera.h"
#include "Octree.h"

using namespace RealRHI;

class Renderer {
public:
	Renderer(Device& device, Swapchain& swapchain);
	~Renderer() = default;

	bool DrawFrame(const FrameContext& frameContext, const Camera& camera);

private:
	Device& m_Device;
	Swapchain& m_Swapchain;
	Ref<Pipeline> m_ComputeRaytracePipeline;
	Ref<Buffer> m_ChunkBuffer;
	std::unique_ptr<TransientResourcePool> m_Pool;

	static constexpr uint32_t OCTREE_DEPTH = 5;
	static constexpr uint32_t CHUNK_SIZE = 1 << OCTREE_DEPTH; // 2^OCTREE_DEPTH
	Octree m_ChunkOctree{ OCTREE_DEPTH };
};
