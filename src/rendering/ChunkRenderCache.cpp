#include "rendering/ChunkRenderCache.h"
#include "world/ChunkStreamer.h"
#include "diagnostics/Profiling.h"
#include <iostream>

using namespace RealRHI;

bool ChunkRenderCache::Apply(const ChunkUpdates& updates) {
    // Render graph uses retain removed buffers until GPU completion.
    for (const auto coord : updates.removed) m_Meshes.erase(coord);
    for (const auto& [coord, mesh] : updates.completed) {
        Ref<Buffer> vertices;
        Ref<Buffer> indices;
        if (!mesh.indices.empty()) {
            MC_PROFILE_ZONE("Upload chunk mesh");
            if (m_Device.CreateBuffer({
                    .size = mesh.vertices.size() * sizeof(VoxelVertex),
                    .usage = BufferUsage::Storage,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.vertices.data(),
                    .debugName = "ChunkVertices",
                }, vertices) != Result::Success ||
                m_Device.CreateBuffer({
                    .size = mesh.indices.size() * sizeof(uint32_t),
                    .usage = BufferUsage::Index,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .initialData = mesh.indices.data(),
                    .debugName = "ChunkIndices",
                }, indices) != Result::Success) {
                std::cerr << "Failed to upload chunk mesh.\n";

                return false;
            }
        }

        auto& chunk = m_Meshes[coord];
        // Publish only a complete upload, also clearing buffers for empty meshes.
        chunk.vertices = std::move(vertices);
        chunk.indices = std::move(indices);
        chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());

    }
    return true;
}
