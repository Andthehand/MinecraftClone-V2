#include "world/World.h"

#include <functional>
#include <glm/common.hpp>

size_t ChunkCoordHash::operator()(ChunkCoord c) const noexcept {
    size_t hash = std::hash<int32_t>{}(c.x);
    for (int32_t component : {c.y, c.z})
        hash ^= std::hash<int32_t>{}(component) + size_t{0x9e3779b9}
            + (hash << 6) + (hash >> 2);
    return hash;
}

ChunkMeshNeighbors World::GetChunkNeighbors(ChunkCoord coord) const {
    ChunkMeshNeighbors result{};
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0 && z == 0) continue;
                const auto it = m_Chunks.find(coord + ChunkCoord(x, y, z));
                if (it != m_Chunks.end())
                    result[ChunkNeighborIndex(x, y, z)] = &it->second.octree;
            }
    return result;
}

bool World::CanMeshChunk(ChunkCoord coord, ChunkCoord player) const {
    if (Distance(coord, player) > Options::World::MESH_RADIUS_CHUNKS || !m_Chunks.contains(coord))
        return false;
    const auto neighbors = GetChunkNeighbors(coord);
    for (size_t i = 0; i < neighbors.size(); ++i)
        if (i != ChunkNeighborIndex(0, 0, 0) && !neighbors[i]) return false;
    return true;
}

ChunkCoord World::ChunkCoordinate(glm::vec3 position) {
    return ChunkCoord(glm::floor(position / float(Options::World::CHUNK_SIZE)));
}

int World::Distance(ChunkCoord a, ChunkCoord b) {
    const auto delta = glm::abs(a - b);
    return glm::max(delta.x, glm::max(delta.y, delta.z));
}

void World::GenerateChunk(ChunkCoord coord) {
    auto [it, inserted] = m_Chunks.try_emplace(coord);
    if (inserted) m_Terrain.Generate(it->second.octree, coord);
}

void World::RemoveChunk(ChunkCoord coord) {
    m_Chunks.erase(coord);
}

void World::MarkMeshed(ChunkCoord coord) {
    m_Chunks.at(coord).meshDirty = false;
}
