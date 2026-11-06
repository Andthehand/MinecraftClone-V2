#pragma once
#include "Octree.h"
#include <vector>
#include <array>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct VoxelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t tile;
    float occlusion = 1.0f;
};

struct ChunkMesh {
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t> indices;
};

// The surrounding 3x3x3 volume, including edge and corner neighbors for AO.
// The center slot is unused. Missing chunks are air for standalone meshes.
constexpr size_t ChunkNeighborIndex(int x, int y, int z) {
    return static_cast<size_t>(((z + 1) * 3 + y + 1) * 3 + x + 1);
}
using ChunkMeshNeighbors = std::array<const Octree*, 27>;

// Greedily merge faces with constant, matching AO and texture. Faces with AO
// gradients stay block-sized. Neighbor octrees must have the same side length.
ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors = {});
