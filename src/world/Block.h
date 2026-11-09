#pragma once

#include <cstdint>

// Stable voxel values shared by terrain generation and meshing.
namespace Blocks {
    enum Id : uint32_t { Air = 0, Grass = 1, Dirt = 2, Stone = 3 };
}
