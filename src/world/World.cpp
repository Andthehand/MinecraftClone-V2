#include "world/World.h"

#include <functional>
#include <glm/common.hpp>

namespace {
    // Integer floor division avoids both negative remainders and float precision loss.
    ChunkCoord BlockChunk(glm::ivec3 p) {
        constexpr int side = Options::World::CHUNK_SIZE;
        const auto divide = [](int v) { return v / side - (v % side < 0 ? 1 : 0); };
        return { divide(p.x), divide(p.y), divide(p.z) };
    }
}

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
    if (inserted) {
        m_Terrain.Generate(it->second.octree, coord);
        it->second.navigationRevision = ++m_NavigationRevision;
    }
}

void World::RemoveChunk(ChunkCoord coord) {
    if (m_Chunks.erase(coord)) ++m_NavigationRevision;
}

void World::MarkMeshed(ChunkCoord coord) {
    m_Chunks.at(coord).meshDirty = false;
}

std::optional<uint32_t> World::GetBlock(glm::ivec3 position) const {
    const auto coord = BlockChunk(position);
    const auto it = m_Chunks.find(coord);
    if (it == m_Chunks.end()) return std::nullopt;
    const auto local = position - coord * int(Options::World::CHUNK_SIZE);
    return it->second.octree.GetVoxel(local.x, local.y, local.z);
}

bool World::SetBlock(glm::ivec3 position, uint32_t block) {
    const auto coord = BlockChunk(position);
    const auto it = m_Chunks.find(coord);
    if (it == m_Chunks.end()) return false;
    const auto local = position - coord * int(Options::World::CHUNK_SIZE);
    if (it->second.octree.GetVoxel(local.x, local.y, local.z) == block) return true;
    it->second.octree.SetVoxel(local.x, local.y, local.z, block);
    it->second.navigationRevision = ++m_NavigationRevision;
    // Meshing samples a one-voxel halo, including diagonal neighbors.
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                const glm::ivec3 offset{x, y, z};
                bool touches = true;
                for (int axis = 0; axis < 3; ++axis)
                    if ((offset[axis] < 0 && local[axis] != 0)
                        || (offset[axis] > 0 && local[axis] != int(Options::World::CHUNK_SIZE) - 1))
                        touches = false;
                if (touches) {
                    const auto neighbor = m_Chunks.find(coord + offset);
                    if (neighbor != m_Chunks.end()) neighbor->second.meshDirty = true;
                }
            }
    return true;
}

uint64_t World::GetNavigationRevision(ChunkCoord coord) const {
    const auto it = m_Chunks.find(coord);
    return it == m_Chunks.end() ? 0 : it->second.navigationRevision;
}
