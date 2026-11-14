#pragma once

#include "world/Block.h"

// CPU face-to-atlas mapping; independent of graphics resources.
namespace BlockFaces {
enum Tile : uint32_t { GrassTop, GrassSide, Dirt, Stone };

// Unknown solid materials use stone until they receive their own texture.
constexpr Tile FaceTile(uint32_t material, int axis, int sign) {
    if (material == Blocks::Grass) {
        return axis == 1 ? (sign > 0 ? GrassTop : Dirt) : GrassSide;
    }

    return material == Blocks::Dirt ? Dirt : Stone;
}

} // namespace BlockFaces
