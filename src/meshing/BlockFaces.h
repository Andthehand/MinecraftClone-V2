#pragma once

#include "world/Block.h"
#include "world/BlockRegistry.h"

// CPU face-to-atlas mapping; independent of graphics resources.
namespace BlockFaces {
inline uint32_t FaceTile(uint32_t material, int axis, int sign) {
    return Blocks::Registry().Get(material).faceTiles[axis * 2 + (sign > 0 ? 1 : 0)];
}

} // namespace BlockFaces
