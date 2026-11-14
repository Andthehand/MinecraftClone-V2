#include "world/World.h"
#include "world/Block.h"

#include <cmath>
#include <functional>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Integer floor division avoids both negative remainders and float precision loss.
ChunkCoord BlockChunk(glm::ivec3 p) {
    constexpr int side = Options::World::CHUNK_SIZE;
    const auto divide = [](int v) { return v / side - (v % side < 0 ? 1 : 0); };

    return {divide(p.x), divide(p.y), divide(p.z)};
}

glm::ivec3 LocalBlockPosition(glm::ivec3 position, ChunkCoord coord) {
    return position - coord * int(Options::World::CHUNK_SIZE);
}
} // namespace

size_t ChunkCoordHash::operator()(ChunkCoord c) const noexcept {
    size_t hash = std::hash<int32_t>{}(c.x);

    for (int32_t component : {c.y, c.z}) {
        hash ^= std::hash<int32_t>{}(component) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
    }

    return hash;
}

ChunkMeshNeighbors World::GetChunkNeighbors(ChunkCoord coord) const {
    ChunkMeshNeighbors result{};

    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0 && z == 0) {
                    // Center chunk is not a neighbor.
                    continue;
                }

                const auto it = m_Chunks.find(coord + ChunkCoord(x, y, z));

                if (it != m_Chunks.end()) {
                    result[ChunkNeighborIndex(x, y, z)] = &it->second.octree;
                }
            }
        }
    }

    return result;
}

bool World::CanMeshChunk(ChunkCoord coord, ChunkCoord player) const {
    if (Distance(coord, player) > Options::World::MESH_RADIUS_CHUNKS || !m_Chunks.contains(coord)) {
        // Chunk is too far away or not loaded, so it cannot be meshed.
        return false;
    }

    const auto neighbors = GetChunkNeighbors(coord);

    for (size_t i = 0; i < neighbors.size(); ++i) {
        // If a neighbor is missing, the chunk cannot be meshed.
        if (i != ChunkNeighborIndex(0, 0, 0) && !neighbors[i]) {
            return false;
        }
    }

    return true;
}

ChunkCoord World::ChunkCoordinate(glm::vec3 position) {
    return ChunkCoord(glm::floor(position / float(Options::World::CHUNK_SIZE)));
}

int World::Distance(ChunkCoord a, ChunkCoord b) {
    const auto delta = glm::abs(a - b);

    return glm::max(delta.x, glm::max(delta.y, delta.z));
}

void World::MarkAffectedMeshesDirty(ChunkCoord coord, glm::ivec3 local) {
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const glm::ivec3 offset{x, y, z};
                bool touches = true;

                for (int axis = 0; axis < 3; ++axis) {
                    if ((offset[axis] < 0 && local[axis] != 0) ||
                        (offset[axis] > 0 && local[axis] != int(Options::World::CHUNK_SIZE) - 1)) {
                        touches = false;
                    }
                }

                if (touches) {
                    const auto neighbor = m_Chunks.find(coord + offset);

                    if (neighbor != m_Chunks.end()) {
                        neighbor->second.meshDirty = true;
                    }
                }
            }
        }
    }
}

void World::GenerateChunk(ChunkCoord coord) {
    auto [it, inserted] = m_Chunks.try_emplace(coord);

    if (inserted) {
        m_Terrain.Generate(it->second.octree, coord);
        it->second.navigationRevision = ++m_NavigationRevision;
    }
}

void World::RemoveChunk(ChunkCoord coord) {
    if (m_Chunks.erase(coord)) {
        ++m_NavigationRevision;
    }
}

void World::ClearMeshDirtyFlag(ChunkCoord coord) {
    m_Chunks.at(coord).meshDirty = false;
}

std::optional<uint32_t> World::GetBlock(glm::ivec3 position) const {
    const auto coord = BlockChunk(position);

    const auto it = m_Chunks.find(coord);

    if (it == m_Chunks.end()) {
        // Chunk does not exist yet, so the block is unknown.
        return std::nullopt;
    }

    const auto local = LocalBlockPosition(position, coord);

    return it->second.octree.GetVoxel(local.x, local.y, local.z);
}

std::optional<BlockHit> World::Raycast(glm::vec3 origin, glm::vec3 direction, float reach) const {
    const float length = glm::length(direction);

    if (!std::isfinite(length) || length <= 0.0f || !std::isfinite(reach) || reach < 0.0f) {
        return std::nullopt;
    }

    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(origin[axis])) {
            return std::nullopt;
        }
    }

    direction /= length;

    glm::ivec3 position(glm::floor(origin));
    glm::ivec3 step(0);
    glm::vec3 next(std::numeric_limits<float>::infinity());
    glm::vec3 delta(std::numeric_limits<float>::infinity());

    for (int axis = 0; axis < 3; ++axis) {
        // If the ray is parallel to this axis, it will never hit a block boundary along this axis.
        if (direction[axis] == 0.0f) {
            continue;
        }

        step[axis] = direction[axis] > 0.0f ? 1 : -1;
        const float boundary = float(position[axis]) + (step[axis] > 0 ? 1.0f : 0.0f);
        next[axis] = (boundary - origin[axis]) / direction[axis];

        delta[axis] = std::abs(1.0f / direction[axis]);
    }

    glm::ivec3 normal(0);
    float distance = 0.0f;

    while (distance <= reach) {
        const auto block = GetBlock(position);

        if (!block) {
            return std::nullopt;
        }

        if (*block != Blocks::Air) {
            return BlockHit{position, normal, distance};
        }

        // Fixed axis ordering makes edge and corner hits deterministic.
        int axis = 0;

        if (next.y < next[axis]) {
            axis = 1;
        }

        if (next.z < next[axis]) {
            axis = 2;
        }

        distance = next[axis];
        position[axis] += step[axis];
        next[axis] += delta[axis];
        normal = glm::ivec3(0);
        normal[axis] = -step[axis];
    }

    return std::nullopt;
}

bool World::SetBlock(glm::ivec3 position, uint32_t block) {
    const auto coord = BlockChunk(position);
    const auto it = m_Chunks.find(coord);

    if (it == m_Chunks.end()) {
        // Chunk does not exist yet...
        return false;
    }

    const auto local = LocalBlockPosition(position, coord);

    if (it->second.octree.GetVoxel(local.x, local.y, local.z) == block) {
        // Block is already set to the desired value, no need to update.
        return true;
    }

    it->second.octree.SetVoxel(local.x, local.y, local.z, block);
    it->second.navigationRevision = ++m_NavigationRevision;

    // Check all 26 neighbors to see if they touch the chunk boundary, and mark them dirty if so.
    MarkAffectedMeshesDirty(coord, local);

    return true;
}

uint64_t World::GetNavigationRevision(ChunkCoord coord) const {
    const auto it = m_Chunks.find(coord);

    return it == m_Chunks.end() ? 0 : it->second.navigationRevision;
}
