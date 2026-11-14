#pragma once

#include <array>
#include <cstdint>
#include <cassert>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Blocks {
struct Definition {
    std::string key;
    std::string name;

    // -X, +X, -Y, +Y, -Z, +Z. Air has no rendered faces.
    std::array<uint32_t, 6> faceTiles{};
};

struct TextureDefinition {
    std::string key;
    std::filesystem::path file;
    std::array<uint8_t, 3> color{};
    std::array<uint8_t, 3> edgeColor{};
    bool grassEdge = false;
};

class BlockRegistry {
public:
    // All validation completes before success. Errors include the source filename.
    [[nodiscard]] static std::expected<BlockRegistry, std::string> Load(const std::filesystem::path& assets);

    // IDs must come from this registry or the reserved built-in IDs.
    const Definition& Get(uint32_t id) const {
        assert(id < m_Blocks.size());

        return m_Blocks[id];
    }

    [[nodiscard]] std::optional<uint32_t> Find(std::string_view key) const;

    const std::vector<Definition>& All() const { return m_Blocks; }

    const std::vector<TextureDefinition>& Textures() const { return m_Textures; }

private:
    std::vector<Definition> m_Blocks;
    std::vector<TextureDefinition> m_Textures;
};

// Install once on the main thread, before starting world/meshing work; immutable thereafter.
[[nodiscard]] std::expected<void, std::string> Initialize(const std::filesystem::path& assets);

const BlockRegistry& Registry();
} // namespace Blocks
