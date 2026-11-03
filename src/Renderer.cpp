#include "Renderer.h"

#include <RenderGraph/RenderGraph.h>

#include <chrono>
#include <iostream>
#include <vector>

Renderer::Renderer(Device& device, Swapchain& swapchain)
	: m_Device(device), m_Swapchain(swapchain) {
	RealRHI::Ref<RealRHI::Shader> shader;
	if (m_Device.CreateShader(ShaderDesc{
			.moduleName = "raytrace.slang",
			.debugName = "RaytraceComputeShader",
		}, shader) != Result::Success) {
		std::cerr << "Failed to create raytrace shader.\n";
		return;
	}
	if (m_Device.CreateComputePipeline(ComputePipelineDesc{
			.shader = shader.Raw(),
			.debugName = "RaytraceComputePipeline",
		}, m_ComputeRaytracePipeline) != Result::Success) {
		std::cerr << "Failed to create raytrace pipeline.\n";
		return;
	}

	m_Pool = std::make_unique<TransientResourcePool>(m_Device, 64ull * 1024 * 1024);

	constexpr uint64_t chunkVolume = static_cast<uint64_t>(CHUNK_SIZE) * CHUNK_SIZE * CHUNK_SIZE;
	std::vector<uint32_t> chunkData(chunkVolume, 0);
	const float center = static_cast<float>(CHUNK_SIZE) * 0.5f;
	const float radius = static_cast<float>(CHUNK_SIZE) * 0.45f;
	const float radiusSquared = radius * radius;
	for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
		for (uint32_t y = 0; y < CHUNK_SIZE; ++y) {
			for (uint32_t x = 0; x < CHUNK_SIZE; ++x) {
				const uint64_t index = x + static_cast<uint64_t>(y) * CHUNK_SIZE +
					static_cast<uint64_t>(z) * CHUNK_SIZE * CHUNK_SIZE;

				const float dx = (static_cast<float>(x) + 0.5f) - center;
				const float dy = (static_cast<float>(y) + 0.5f) - center;
				const float dz = (static_cast<float>(z) + 0.5f) - center;
				const float distanceSquared = dx * dx + dy * dy + dz * dz;

				chunkData[index] = distanceSquared <= radiusSquared ? 1u : 0u;
			}
		}
	}

	m_Device.CreateBuffer(BufferDesc{
		.size = chunkVolume * sizeof(uint32_t),
		.usage = BufferUsage::Storage,
		.memoryUsage = MemoryUsage::CPUToGPU,
		.initialData = chunkData.data(),
		.debugName = "ChunkBuffer",
	}, m_ChunkBuffer);
}

struct Parameters {
	uint32_t width;
	uint32_t height;
	uint32_t chunkSize;
	float time;

	uint32_t chunkBufferIndex;
	uint32_t backBufferIndex;
};

bool Renderer::DrawFrame(const FrameContext& frameContext) {
	RenderGraph graph;

	RenderGraphResource chunkBufferResource = graph.ImportBuffer(m_ChunkBuffer.Raw());
	RenderGraphResource backBufferResource = graph.ImportBackBuffer(m_Swapchain, frameContext);

	constexpr ResourceState computeRead{ PipelineStage::ComputeShader, ResourceAccess::ShaderRead };
	constexpr ResourceState computeWrite{ PipelineStage::ComputeShader, ResourceAccess::ShaderWrite };

	graph.AddPass(RenderGraphPassDesc{
		.name = "RaytracePass",
		.resources = {
			RenderGraphUse{ chunkBufferResource, computeRead, TextureLayout::Undefined, RenderGraphAccess::Read },
			RenderGraphUse{ backBufferResource, computeWrite, TextureLayout::General, RenderGraphAccess::Write },
		},
	}, [&](CommandList& commands) {
		static const auto startTime = std::chrono::system_clock::now();
		auto now = std::chrono::system_clock::now();
		float time = std::chrono::duration<float>(now - startTime).count();

		Parameters params{
			.width = frameContext.width,
			.height = frameContext.height,
			.chunkSize = CHUNK_SIZE,
			.time = time,
			.chunkBufferIndex = m_ChunkBuffer->GetDescriptorIndex(),
			.backBufferIndex = graph.GetTexture(backBufferResource)->GetStorageImageDescriptorIndex(),
		};

		commands.BindPipeline(m_ComputeRaytracePipeline.Raw());
		commands.SetPushConstants(&params, sizeof(params));
		// TODO: Query the pipeline reflection for the workgroup size instead of hardcoding 16x16.
		commands.DispatchCompute((frameContext.width + 15) / 16, (frameContext.height + 15) / 16, 1);
	});

	RenderGraphSubmission submission;
	if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
		std::cerr << "Render graph submission failed.\n";
		return false;
	}

	return true;
}
