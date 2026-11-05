#include "Renderer.h"

#include <RenderGraph/RenderGraph.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <print>

namespace {
	const std::vector<ChunkCoord>& GetChunkLoadOffsets() {
		static const std::vector<ChunkCoord> OFFSETS = [] {
			constexpr int LOAD_RADIUS = Options::World::LOAD_RADIUS;
			constexpr int LOAD_DIAMETER = 2 * LOAD_RADIUS + 1;
			std::vector<ChunkCoord> offsets;
			offsets.reserve(size_t{LOAD_DIAMETER} * LOAD_DIAMETER * LOAD_DIAMETER);
			for (int z = -LOAD_RADIUS; z <= LOAD_RADIUS; ++z) {
				for (int y = -LOAD_RADIUS; y <= LOAD_RADIUS; ++y) {
					for (int x = -LOAD_RADIUS; x <= LOAD_RADIUS; ++x) {
						offsets.push_back({x, y, z});
					}
				}
			}
			// Preserve z/y/x ordering within each distance shell.
			std::stable_sort(offsets.begin(), offsets.end(), [](ChunkCoord a, ChunkCoord b) {
				return World::Distance(a, {0, 0, 0}) < World::Distance(b, {0, 0, 0});
			});
			return offsets;
		}();
		return OFFSETS;
	}
}

Renderer::Renderer(Device& device, Swapchain& swapchain)
	: m_Device(device), m_Swapchain(swapchain),
	  m_NodeAllocator(Options::World::NODE_POOL_CAPACITY, static_cast<uint32_t>(
		std::max<uint64_t>(Options::Rendering::MIN_NODE_ALLOCATION_ALIGNMENT_BYTES, device.GetCapabilities().limits.nonCoherentAtomSize) / sizeof(GpuOctreeNode))) {
	
	RealRHI::Ref<RealRHI::Shader> shader;
	if (m_Device.CreateShader(ShaderDesc{
			.moduleName = Options::Rendering::SHADER_MODULE,
			.debugName = "RaytraceComputeShader",
		}, shader) != Result::Success) {
		std::cerr << "Failed to create raytrace shader.\n";
		return;
	}
	for (const auto& entry : shader->GetReflection().entryPoints) {
		if (entry.stage == ShaderStage::Compute) {
			m_RaytraceWorkgroupSize = entry.computeGroupSize;
			break;
		}
	}
	if (m_RaytraceWorkgroupSize[0] == 0 || m_RaytraceWorkgroupSize[1] == 0 || m_RaytraceWorkgroupSize[2] != 1) {
		std::cerr << "Raytrace shader requires a two-dimensional compute workgroup.\n";
		return;
	}

	if (m_Device.CreateComputePipeline(ComputePipelineDesc{
			.shader = shader.Raw(),
			.debugName = "RaytraceComputePipeline",
		}, m_ComputeRaytracePipeline) != Result::Success) {
		std::cerr << "Failed to create raytrace pipeline.\n";
		return;
	}
	m_Pool = std::make_unique<TransientResourcePool>(m_Device, Options::Rendering::TRANSIENT_POOL_BYTES);

	if (m_Device.CreateBuffer(BufferDesc{
		.size = uint64_t{ Options::World::NODE_POOL_CAPACITY} * sizeof(GpuOctreeNode),
		.usage = BufferUsage::Storage,
		.memoryUsage = MemoryUsage::CPUToGPU,
		.debugName = "ChunkNodePool",
	}, m_ChunkBuffer) != Result::Success) {
		std::cerr << "Failed to create chunk node pool.\n";
		return;
	}
}

bool Renderer::UpdateChunks(Vec3 position) {
	const ChunkCoord playerPos = World::ChunkCoordinate(position.x, position.y, position.z);
	const ChunkCoord origin{playerPos.x - Options::World::UNLOAD_RADIUS,
							playerPos.y - Options::World::UNLOAD_RADIUS,
							playerPos.z - Options::World::UNLOAD_RADIUS};
	bool changed = !m_ChunkTable || !(origin == m_GridOrigin);
	m_GridOrigin = origin;

	// Defer chunk freeing until after the GPU has finished reading them, to avoid a GPU wait.
	for (auto it = m_World.chunks.begin(); it != m_World.chunks.end();) {
		if (World::Distance(it->first, playerPos) > Options::World::UNLOAD_RADIUS) {
			m_RetiredAllocations.push_back({it->second.allocation, m_LastSubmission});
			it = m_World.chunks.erase(it);
			changed = true;
		} else {
			++it;
		}
	}

	// Free retired allocations that are no longer in use by the GPU.
	for (auto it = m_RetiredAllocations.begin(); it != m_RetiredAllocations.end();) {
		if (it->lastUse.IsComplete(m_Device)) {
			if (!m_NodeAllocator.Free(it->allocation)) {
				std::cerr << "Invalid retired chunk allocation.\n";
				return false;
			}
			it = m_RetiredAllocations.erase(it);
		} else {
			++it;
		}
	}

	// Visit cached offsets in nearest-first order; only successful loads use the budget.
	uint32_t loaded = 0;
	for (const ChunkCoord offset : GetChunkLoadOffsets()) {
		if (loaded == Options::World::CHUNKS_LOADED_PER_FRAME) {
			break;
		}

		const ChunkCoord coord{playerPos.x + offset.x, playerPos.y + offset.y, playerPos.z + offset.z};
		if (m_World.chunks.contains(coord)) {
			continue;
		}

		const auto it = m_World.chunks.try_emplace(coord).first;
		auto& chunk = it->second;
		World::Generate(chunk.octree);
		const auto nodes = chunk.octree.ToGpuNodes();
		const auto allocation = m_NodeAllocator.Allocate(static_cast<uint32_t>(nodes.size()));
		if (!allocation) {
			m_World.chunks.erase(it);
			break; // Retry next frame after retired ranges become available.
		}

		if (m_ChunkBuffer->WriteData(nodes.data(), nodes.size() * sizeof(GpuOctreeNode),
			uint64_t{allocation->offset} * sizeof(GpuOctreeNode)) != Result::Success) {
			m_NodeAllocator.Free(*allocation);
			m_World.chunks.erase(it);
			std::cerr << "Failed to upload chunk.\n";
			return false;
		}

		chunk.allocation = *allocation;
		++loaded;
		changed = true;
	}

	if (!changed) {
		return true;
	}
	std::vector<uint32_t> table(Options::World::GRID_CELLS, Options::World::MISSING_CHUNK);
	for (const auto& [coord, chunk] : m_World.chunks) {
		table[World::VoxelIndex(coord.x - origin.x, coord.y - origin.y, coord.z - origin.z)] =
			chunk.allocation.offset;
	}
	// Publish an immutable snapshot. RHI command lists retain older buffers until
	// their GPU submissions complete, so no in-flight lookup table is overwritten.
	Ref<Buffer> nextTable;
	if (m_Device.CreateBuffer(BufferDesc{
			.size = table.size() * sizeof(uint32_t),
			.usage = BufferUsage::Storage,
			.memoryUsage = MemoryUsage::CPUToGPU,
			.initialData = table.data(),
			.debugName = "ChunkLookupTable",
		}, nextTable) != Result::Success) {
		std::cerr << "Failed to create chunk lookup table.\n";
		return false;
	}
	std::println("Chunk lookup table updated: {} chunks loaded, {} retired allocations.", m_World.chunks.size(), m_RetiredAllocations.size());
	m_ChunkTable = nextTable;
	return true;
}

struct Parameters {
	uint32_t width;
	uint32_t height;
	uint32_t chunkSize;
	uint32_t chunkTableIndex;

	float cameraPosition[4];
	float cameraForward[4];
	float cameraRight[4];
	float cameraUp[4];

	float tanHalfFovY;
	float aspect;
	uint32_t chunkBufferIndex;
	uint32_t backBufferIndex;
	int32_t gridOrigin[4];
	uint32_t gridSize[4];
};
static_assert(sizeof(Parameters) == 128);

bool Renderer::DrawFrame(const FrameContext& frameContext, const Camera& camera) {
	RenderGraph graph;

	RenderGraphResource chunkBufferResource = graph.ImportBuffer(m_ChunkBuffer.Raw());
	RenderGraphResource tableResource = graph.ImportBuffer(m_ChunkTable.Raw());
	RenderGraphResource backBufferResource = graph.ImportBackBuffer(m_Swapchain, frameContext);

	constexpr ResourceState COMPUTE_READ{ PipelineStage::ComputeShader, ResourceAccess::ShaderRead };
	constexpr ResourceState COMPUTE_WRITE{ PipelineStage::ComputeShader, ResourceAccess::ShaderWrite };

	graph.AddPass(RenderGraphPassDesc{
		.name = "RaytracePass",
		.resources = {
			RenderGraphUse{ chunkBufferResource, COMPUTE_READ, TextureLayout::Undefined, RenderGraphAccess::Read },
			RenderGraphUse{ tableResource, COMPUTE_READ, TextureLayout::Undefined, RenderGraphAccess::Read },
			RenderGraphUse{ backBufferResource, COMPUTE_WRITE, TextureLayout::General, RenderGraphAccess::Write },
		},
	}, [&](CommandList& commands) {
		commands.UseResource(m_ChunkBuffer.Raw());
		commands.UseResource(m_ChunkTable.Raw());
		const Vec3 position = camera.GetPosition();
		const Vec3 forward = camera.GetForward();
		const Vec3 right = camera.GetRight();
		const Vec3 up = camera.GetUp();

		const float aspect = frameContext.height == 0 ? 1.0f : static_cast<float>(frameContext.width) / static_cast<float>(frameContext.height);
		const float tanHalfFovY = std::tan(camera.GetFovYRadians() * 0.5f);

		Parameters params{
			.width = frameContext.width,
			.height = frameContext.height,
			.chunkSize = Options::World::CHUNK_SIZE,
			.chunkTableIndex = m_ChunkTable->GetDescriptorIndex(),
			.cameraPosition = { position.x, position.y, position.z, 0.0f },
			.cameraForward = { forward.x, forward.y, forward.z, 0.0f },
			.cameraRight = { right.x, right.y, right.z, 0.0f },
			.cameraUp = { up.x, up.y, up.z, 0.0f },
			.tanHalfFovY = tanHalfFovY,
			.aspect = aspect,
			.chunkBufferIndex = m_ChunkBuffer->GetDescriptorIndex(),
			.backBufferIndex = graph.GetTexture(backBufferResource)->GetStorageImageDescriptorIndex(),
			.gridOrigin = {m_GridOrigin.x, m_GridOrigin.y, m_GridOrigin.z, 0},
			.gridSize = {Options::World::GRID_SIZE, Options::World::GRID_SIZE, Options::World::GRID_SIZE, 0},
		};

		commands.BindPipeline(m_ComputeRaytracePipeline.Raw());
		commands.SetPushConstants(&params, sizeof(params));
		// Read the dimensions from the compiled shader so shader options can change independently.
		commands.DispatchCompute((frameContext.width + m_RaytraceWorkgroupSize[0] - 1) / m_RaytraceWorkgroupSize[0],
			(frameContext.height + m_RaytraceWorkgroupSize[1] - 1) / m_RaytraceWorkgroupSize[1], 1);
	});

	RenderGraphSubmission submission;
	if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
		std::cerr << "Render graph submission failed.\n";
		return false;
	}

	m_LastSubmission = std::move(submission);
	return true;
}
