#include "world/BlockRegistry.h"
#include "world/Block.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Blocks {
namespace {
using Json = nlohmann::json;
std::unique_ptr<const BlockRegistry> registry;

const Json& Member(const Json& object, std::string_view key) {
    static const Json missing;

    if (!object.is_object()) {
        return missing;
    }

    const auto found = object.find(key);

    return found != object.end() ? *found : missing;
}

std::expected<Json, std::string> Read(const std::filesystem::path& path) {
    std::ifstream stream(path);

    if (!stream) {
        return std::unexpected("cannot open file");
    }

    std::vector<std::unordered_set<std::string>> keys;
    std::optional<std::string> duplicate;
    auto document = Json::parse(
        stream,
        [&](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
            } else if (event == Json::parse_event_t::key) {
                const auto& key = parsed.get_ref<const std::string&>();

                if (!keys.back().insert(key).second && !duplicate) {
                    duplicate = key;
                }
            }

            return true;
        },
        false);

    if (stream.bad()) {
        return std::unexpected("cannot read file");
    }

    if (document.is_discarded()) {
        return std::unexpected("invalid JSON");
    }

    if (duplicate) {
        return std::unexpected("duplicate JSON key '" + *duplicate + "'");
    }

    return document;
}

// Keep the first validation error so related fields can be checked without unsafe JSON conversions.
struct Validation {
    std::filesystem::path source;
    std::string error;

    bool Check(bool condition, std::string message) {
        if (!condition && error.empty()) {
            error = source.string() + ": " + std::move(message);
        }

        return condition;
    }

    bool Fields(const Json& value, std::initializer_list<std::string_view> allowed) {
        if (!Check(value.is_object(), "expected an object")) {
            return false;
        }

        for (const auto& [key, unused] : value.items()) {
            if (!Check(std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
                       "unknown field '" + key + "'")) {
                return false;
            }
        }

        return true;
    }

    std::string Text(const Json& value) {
        if (!Check(value.is_string(), "expected a nonempty string")) {
            return {};
        }

        const auto& text = value.get_ref<const std::string&>();
        Check(!text.empty(), "strings must not be empty");

        return text;
    }

    std::string Key(const Json& value) {
        auto key = Text(value);
        const auto colon = key.find(':');
        Check(colon != std::string::npos && colon != 0 && colon + 1 != key.size() &&
                  key.find(':', colon + 1) == std::string::npos &&
                  key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_:-./") == std::string::npos,
              "invalid namespaced id '" + key + "'");

        return key;
    }

    bool Version(const Json& value) {
        const auto& version = Member(value, "schema_version");

        return Check(version.is_number_integer() && version == 1, "schema_version must be integer 1");
    }

    std::array<uint8_t, 3> Color(const Json& value) {
        std::array<uint8_t, 3> result{};

        if (!Check(value.is_array() && value.size() == 3, "color must contain three integer RGB channels")) {
            return result;
        }

        for (size_t i = 0; i < result.size(); ++i) {
            if (!Check(value[i].is_number_integer() && value[i] >= 0 && value[i] <= 255,
                       "RGB channels must be integers in [0, 255]")) {
                return result;
            }

            result[i] = value[i].get<uint8_t>();
        }

        return result;
    }
};
} // namespace

std::expected<BlockRegistry, std::string> BlockRegistry::Load(const std::filesystem::path& assets) {
    BlockRegistry result;
    Validation validation{assets / "textures.json", {}};
    auto document = Read(validation.source);

    if (!document) {
        return std::unexpected(validation.source.string() + ": " + document.error());
    }

    if (!validation.Fields(*document, {"schema_version", "textures"}) || !validation.Version(*document)) {
        return std::unexpected(validation.error);
    }

    const auto& textures = Member(*document, "textures");

    if (!validation.Check(textures.is_object() && !textures.empty(), "textures must be a nonempty object")) {
        return std::unexpected(validation.error);
    }

    std::unordered_map<std::string, uint32_t> tiles;

    for (const auto& [key, value] : textures.items()) {
        TextureDefinition texture;
        texture.key = validation.Key(Json(key));
        validation.Fields(value, {"file", "color", "edge_color"});

        if (!validation.error.empty()) {
            return std::unexpected(validation.error);
        }

        if (value.contains("file")) {
            validation.Check(value.size() == 1, key + ": file cannot be combined with procedural colors");
            const std::filesystem::path relative = validation.Text(Member(value, "file"));
            validation.Check(!relative.has_root_path() &&
                                 std::find(relative.begin(), relative.end(), "..") == relative.end() &&
                                 relative.extension() == ".png",
                             key + ": file must be a relative .png path inside assets");

            if (!validation.error.empty()) {
                return std::unexpected(validation.error);
            }

            texture.file = assets / relative;
            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(texture.file, error);
            validation.Check(!error && exists, "missing texture '" + texture.file.string() + "'");
        } else {
            texture.color = validation.Color(Member(value, "color"));

            if (value.contains("edge_color")) {
                texture.edgeColor = validation.Color(Member(value, "edge_color"));
                texture.grassEdge = true;
            }
        }

        // Bound the horizontal 16px atlas to 16384px in width.
        validation.Check(result.m_Textures.size() < 1024, "at most 1024 textures are supported");

        if (!validation.error.empty()) {
            return std::unexpected(validation.error);
        }

        tiles.emplace(key, static_cast<uint32_t>(result.m_Textures.size()));
        result.m_Textures.push_back(std::move(texture));
    }

    validation.source = assets / "blocks";
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::directory_iterator entry(validation.source, error), end;

    while (!error && entry != end) {
        const bool regular = entry->is_regular_file(error);

        if (!error && regular && entry->path().extension() == ".json") {
            files.push_back(entry->path());
        }

        if (!error) {
            entry.increment(error);
        }
    }

    if (error) {
        return std::unexpected(validation.source.string() + ": " + error.message());
    }

    std::sort(files.begin(), files.end());
    std::unordered_map<std::string, Definition> definitions;

    for (const auto& file : files) {
        validation.source = file;
        auto block = Read(file);

        if (!block) {
            return std::unexpected(file.string() + ": " + block.error());
        }

        if (!validation.Fields(*block, {"schema_version", "id", "name", "textures"}) || !validation.Version(*block)) {
            return std::unexpected(validation.error);
        }

        Definition definition;
        definition.key = validation.Key(Member(*block, "id"));
        definition.name = validation.Text(Member(*block, "name"));

        if (!validation.error.empty()) {
            return std::unexpected(validation.error);
        }

        if (definition.key == "core:air") {
            validation.Check(!block->contains("textures"), "air must not specify textures");
        } else {
            const auto& faces = Member(*block, "textures");

            if (!validation.Fields(faces, {"all", "left", "right", "bottom", "top", "back", "front"})) {
                return std::unexpected(validation.error);
            }

            for (const auto& [face, value] : faces.items()) {
                const auto key = validation.Text(value);
                validation.Check(tiles.contains(key), "unknown texture '" + key + "'");
            }

            if (!validation.error.empty()) {
                return std::unexpected(validation.error);
            }

            constexpr std::array names{"left", "right", "bottom", "top", "back", "front"};

            for (size_t face = 0; face < names.size(); ++face) {
                const auto key = validation.Text(Member(faces, faces.contains(names[face]) ? names[face] : "all"));
                const auto tile = tiles.find(key);
                validation.Check(tile != tiles.end(), "unknown texture '" + key + "'");

                if (!validation.error.empty()) {
                    return std::unexpected(validation.error);
                }

                definition.faceTiles[face] = tile->second;
            }
        }

        if (!validation.error.empty()) {
            return std::unexpected(validation.error);
        }

        const auto key = definition.key;

        if (!validation.Check(definitions.emplace(key, std::move(definition)).second,
                              "duplicate block id '" + key + "'")) {
            return std::unexpected(validation.error);
        }
    }

    validation.source = assets / "blocks";

    // Retain terrain generation's original numeric IDs, independent of filenames/order.
    for (const char* key : {"core:air", "core:grass", "core:dirt", "core:stone"}) {
        const auto found = definitions.find(key);

        if (!validation.Check(found != definitions.end(), std::string("missing required block '") + key + "'")) {
            return std::unexpected(validation.error);
        }

        result.m_Blocks.push_back(std::move(found->second));
        definitions.erase(found);
    }

    std::vector<std::string> remaining;

    for (const auto& [key, unused] : definitions) {
        remaining.push_back(key);
    }

    std::sort(remaining.begin(), remaining.end());

    for (const auto& key : remaining) {
        result.m_Blocks.push_back(std::move(definitions.find(key)->second));
    }

    return result;
}

std::optional<uint32_t> BlockRegistry::Find(std::string_view key) const {
    for (size_t id = 0; id < m_Blocks.size(); ++id) {
        if (m_Blocks[id].key == key) {
            return static_cast<uint32_t>(id);
        }
    }

    return std::nullopt;
}

std::expected<void, std::string> Initialize(const std::filesystem::path& assets) {
    if (registry) {
        return std::unexpected("block registry is already initialized");
    }

    auto loaded = BlockRegistry::Load(assets);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }

    registry = std::make_unique<const BlockRegistry>(std::move(*loaded));
    return {};
}

const BlockRegistry& Registry() {
    assert(registry && "block registry must be initialized before use");

    return *registry;
}
} // namespace Blocks
