#pragma once

#include "world/ChunkCoord.h"
#include <Device.h>
#include <unordered_map>

struct ChunkUpdates;

struct ChunkRenderMesh {
    RealRHI::Ref<RealRHI::Buffer> vertices;
    RealRHI::Ref<RealRHI::Buffer> indices;
    uint32_t indexCount = 0;
};

class ChunkRenderCache {
public:
    explicit ChunkRenderCache(RealRHI::Device& device) : m_Device(device) {}
    bool Apply(const ChunkUpdates& updates);
    [[nodiscard]] auto& GetMeshes() { return m_Meshes; }

private:
    RealRHI::Device& m_Device;
    std::unordered_map<ChunkCoord, ChunkRenderMesh, ChunkCoordHash> m_Meshes;
};
