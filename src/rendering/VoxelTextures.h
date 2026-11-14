#pragma once
#include "world/BlockRegistry.h"
#include <cstdint>
#include <vector>

namespace VoxelTextures {
inline constexpr uint32_t TILE_SIZE = 16;
// PNGs must be opaque 16x16 images. Procedural textures use the same atlas layout.
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> BuildAtlas(const Blocks::BlockRegistry& registry);
} // namespace VoxelTextures
