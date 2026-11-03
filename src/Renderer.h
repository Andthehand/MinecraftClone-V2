#include <Device.h>
#include <RenderGraph/TransientResourcePool.h>

using namespace RealRHI;

class Renderer {
public:
	Renderer(Device& device, Swapchain& swapchain);
	~Renderer() = default;

	bool DrawFrame(const FrameContext& frameContext);

private:
	constexpr static uint32_t CHUNK_SIZE = 128;

	Device& m_Device;
	Swapchain& m_Swapchain;
	Ref<Pipeline> m_ComputeRaytracePipeline;
	Ref<Buffer> m_ChunkBuffer;
	std::unique_ptr<TransientResourcePool> m_Pool;
};
