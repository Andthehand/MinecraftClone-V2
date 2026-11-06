#include "World.h"

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
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign : {-1, 1}) {
            auto neighbor = coord;
            neighbor[axis] += sign;
            const auto it = chunks.find(neighbor);
            if (it != chunks.end())
                result[axis * 2 + (sign > 0 ? 1 : 0)] = &it->second.octree;
        }
    }
    return result;
}

bool World::CanMeshChunk(ChunkCoord coord, ChunkCoord player) const {
    if (Distance(coord, player) > Options::World::MESH_RADIUS || !chunks.contains(coord))
        return false;
    const auto neighbors = GetChunkNeighbors(coord);
    for (const auto* neighbor : neighbors)
        if (!neighbor) return false;
    return true;
}

ChunkCoord World::ChunkCoordinate(glm::vec3 position) {
    return ChunkCoord(glm::floor(position / float(Options::World::CHUNK_SIZE)));
}

int World::Distance(ChunkCoord a, ChunkCoord b) {
    const auto delta = glm::abs(a - b);
    return glm::max(delta.x, glm::max(delta.y, delta.z));
}

void World::Generate(Octree& octree, ChunkCoord coord) const {
    m_Terrain.Generate(octree, coord);
}
