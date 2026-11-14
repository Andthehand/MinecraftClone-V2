#pragma once

#include "meshing/BlockFaces.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelTextures {
inline constexpr uint32_t TILE_SIZE = 16;
inline constexpr uint32_t TILE_COUNT = 4;
inline constexpr uint32_t WIDTH = TILE_SIZE * TILE_COUNT;
inline constexpr uint32_t HEIGHT = TILE_SIZE;

// Small, deterministic pixel-art tiles; no external assets or image decoder needed.
inline auto BuildAtlas() {
    std::array<uint8_t, WIDTH * HEIGHT * 4> pixels{};

    for (uint32_t tile = 0; tile < TILE_COUNT; ++tile) {
        for (uint32_t y = 0; y < TILE_SIZE; ++y) {
            for (uint32_t x = 0; x < TILE_SIZE; ++x) {
                uint32_t hash = x * 374761393u + y * 668265263u + 1274126177u;
                hash = (hash ^ (hash >> 13)) * 1274126177u;
                hash ^= hash >> 16;
                const int variation = static_cast<int>(hash % 33) - 16;
                const bool grass =
                    tile == BlockFaces::GrassTop || (tile == BlockFaces::GrassSide && y < 3 + ((x * 7 + x / 3) % 3));
                const bool stone = tile == BlockFaces::Stone;
                const std::array<int, 3> base = grass   ? std::array{82, 146, 58}
                                                : stone ? std::array{128, 130, 133}
                                                        : std::array{126, 86, 54};
                const size_t offset = (y * WIDTH + tile * TILE_SIZE + x) * 4;

                for (size_t channel = 0; channel < 3; ++channel) {
                    pixels[offset + channel] = static_cast<uint8_t>(base[channel] + variation);
                }

                pixels[offset + 3] = 255;
            }
        }
    }

    return pixels;
}
} // namespace VoxelTextures
