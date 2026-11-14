#include "rendering/VoxelTextures.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

namespace VoxelTextures {
std::expected<std::vector<uint8_t>, std::string> BuildAtlas(const Blocks::BlockRegistry& registry) {
    const auto& textures = registry.Textures();
    const size_t width = TILE_SIZE * textures.size();
    std::vector<uint8_t> pixels(width * TILE_SIZE * 4);

    for (size_t tile = 0; tile < textures.size(); ++tile) {
        const auto& texture = textures[tile];
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image(nullptr, stbi_image_free);

        if (!texture.file.empty()) {
            std::ifstream stream(texture.file, std::ios::binary);
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
            const auto fail = [&](const std::string& reason) {
                return std::unexpected(texture.file.string() + ": " + reason);
            };

            if (!stream || bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return fail("cannot read PNG");
            }

            int w = 0, h = 0, channels = 0;

            if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
                w != TILE_SIZE || h != TILE_SIZE) {
                return fail("expected a 16x16 PNG");
            }

            image.reset(stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4));

            if (!image) {
                return fail("cannot decode PNG");
            }

            for (size_t i = 3; i < TILE_SIZE * TILE_SIZE * 4; i += 4) {
                if (image.get()[i] != 255) {
                    return fail("only opaque textures are supported");
                }
            }
        }

        for (uint32_t y = 0; y < TILE_SIZE; ++y) {
            for (uint32_t x = 0; x < TILE_SIZE; ++x) {
                const size_t offset = (y * width + tile * TILE_SIZE + x) * 4;

                if (image) {
                    std::copy_n(image.get() + (y * TILE_SIZE + x) * 4, 4, pixels.data() + offset);
                    continue;
                }

                uint32_t hash = x * 374761393u + y * 668265263u + 1274126177u;
                hash = (hash ^ (hash >> 13)) * 1274126177u;
                hash ^= hash >> 16;
                const int variation = static_cast<int>(hash % 33) - 16;
                const bool edge = texture.grassEdge && y < 3 + ((x * 7 + x / 3) % 3);
                const auto& color = edge ? texture.edgeColor : texture.color;

                for (size_t channel = 0; channel < 3; ++channel) {
                    pixels[offset + channel] = static_cast<uint8_t>(std::clamp(color[channel] + variation, 0, 255));
                }

                pixels[offset + 3] = 255;
            }
        }
    }

    return pixels;
}
} // namespace VoxelTextures
