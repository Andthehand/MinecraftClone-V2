#pragma once

#include "world/Octree.h"
#include <array>

// The surrounding 3x3x3 volume, including edge and corner neighbors for AO.
// The center slot is unused. Missing chunks are air for standalone meshes.
constexpr size_t ChunkNeighborIndex(int x, int y, int z) {
    return static_cast<size_t>(((z + 1) * 3 + y + 1) * 3 + x + 1);
}
using ChunkMeshNeighbors = std::array<const Octree*, 27>;

