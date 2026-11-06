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
};

struct ChunkMesh {
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t> indices;
};

// Face neighbors in -X, +X, -Y, +Y, -Z, +Z order. Missing chunks are air.
using ChunkMeshNeighbors = std::array<const Octree*, 6>;

// Greedily merge coplanar faces with the same texture, including partially
// exposed chunk boundaries. Neighbor octrees must have the same side length.
ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors = {});
