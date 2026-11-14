#include "world/BlockRegistry.h"
#include "world/Block.h"
#include "meshing/BlockFaces.h"
#include "meshing/ChunkMesher.h"
#include "world/Octree.h"
#include "rendering/VoxelTextures.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace {
using Json = nlohmann::json;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Write(const std::filesystem::path& path, const Json& value) {
    std::ofstream stream(path);
    stream << value.dump(2);

    if (!stream) {
        Require(false, "cannot write test fixture");
    }
}

Json Read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    auto document = Json::parse(stream, nullptr, false);

    Require(!document.is_discarded(), "cannot read test fixture");

    return document;
}

template <class T> T Take(std::expected<T, std::string> result) {
    if (!result) {
        Require(false, result.error().c_str());
    }

    return std::move(*result);
}

template <class Function> void Reject(Function function, std::string_view expected) {
    const auto result = function();
    Require(!result, "invalid content was accepted");
    Require(result.error().find(expected) != std::string::npos, "error did not contain expected context");
}

void Remove(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    Require(!error, "cannot remove test fixture");
}

void CopyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "cannot copy test fixture");
}

struct Fixture {
    std::filesystem::path root;

    explicit Fixture(const std::filesystem::path& source) {
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        Require(!error, "cannot locate temporary directory");

        root = temporary /
               ("minecraft-block-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root, error);
        Require(!error, "cannot create fixture directory");

        std::filesystem::copy(source, root, std::filesystem::copy_options::recursive, error);
        Require(!error, "cannot copy fixture directory");
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
} // namespace

int main(int argc, char** argv) {
    Require(argc == 2, "expected assets path");
    Fixture fixture(argv[1]);
    const auto load = [&] { return Blocks::BlockRegistry::Load(fixture.root); };
    const auto original = Take(load());
    Require(!original.Find("core:missing"), "unknown block lookup must return no ID");

    Require(original.Find("core:air") == Blocks::Air && original.Find("core:grass") == Blocks::Grass &&
                original.Find("core:dirt") == Blocks::Dirt && original.Find("core:stone") == Blocks::Stone,
            "built-in IDs changed");
    const auto& grass = original.Get(Blocks::Grass);
    Require(grass.faceTiles[0] == grass.faceTiles[1] && grass.faceTiles[0] == grass.faceTiles[4] &&
                grass.faceTiles[4] == grass.faceTiles[5] && grass.faceTiles[2] != grass.faceTiles[3] &&
                grass.faceTiles[2] == original.Get(Blocks::Dirt).faceTiles[0],
            "grass face overrides failed");

    const auto pixels = Take(VoxelTextures::BuildAtlas(original));
    Require(pixels.size() == 64 * 16 * 4, "built-in atlas size changed");

    const auto grassFile = fixture.root / "blocks/grass.json";
    const auto valid = Read(grassFile);

    for (const Json malformed : {Json(nullptr), Json::array(), Json(42), Json("wrong type")}) {
        Write(grassFile, malformed);
        Reject(load, "expected an object");
    }

    for (const char* field : {"schema_version", "id", "name", "textures"}) {
        auto missing = valid;
        missing.erase(field);
        Write(grassFile, missing);
        Reject(load, "grass.json");
    }

    for (const char* field : {"id", "name"}) {
        auto wrongType = valid;
        wrongType[field] = 42;
        Write(grassFile, wrongType);
        Reject(load, "expected a nonempty string");
    }

    auto wrongFaceType = valid;
    wrongFaceType["textures"]["top"] = false;
    Write(grassFile, wrongFaceType);
    Reject(load, "expected a nonempty string");

    auto invalid = valid;
    invalid["schema_version"] = 2;
    Write(grassFile, invalid);
    Reject(load, "grass.json");

    invalid = valid;
    invalid["solid"] = false;
    Write(grassFile, invalid);
    Reject(load, "unknown field 'solid'");

    invalid = valid;
    invalid["textures"]["all"] = "core:missing";
    Write(grassFile, invalid);
    Reject(load, "unknown texture");

    invalid = valid;
    invalid["textures"].erase("all");
    Write(grassFile, invalid);
    Reject(load, "grass.json");
    Write(grassFile, valid);
    Write(fixture.root / "blocks/duplicate.json", valid);
    Reject(load, "duplicate block id");

    Remove(fixture.root / "blocks/duplicate.json");
    {
        std::ofstream stream(grassFile);
        stream << "{ invalid json";
    }

    Reject(load, "grass.json");
    {
        std::ofstream stream(grassFile);
        stream << R"({"id":"core:grass","id":"core:other"})";
    }

    Reject(load, "duplicate JSON key");

    invalid = valid;
    invalid["id"] = "invalid";
    Write(grassFile, invalid);
    Reject(load, "invalid namespaced id");

    Remove(grassFile);
    Reject(load, "missing required block 'core:grass'");
    Write(grassFile, valid);

    const auto texturesFile = fixture.root / "textures.json";
    const auto validTextures = Read(texturesFile);
    auto textures = validTextures;
    textures["textures"]["core:stone"]["color"] = {256, 0, 0};
    Write(texturesFile, textures);
    Reject(load, "RGB channels");

    textures = validTextures;
    textures["textures"]["core:test"] = {{"file", "missing.png"}};
    Write(texturesFile, textures);
    Reject(load, "missing texture");
    textures["textures"]["core:test"] = {{"file", "../outside.png"}};
    Write(texturesFile, textures);
    Reject(load, "relative .png path");

    // A tiny opaque RGBA PNG exercises the PNG decoder's size validation.
    constexpr unsigned char png[] = {137, 80,  78, 71,  13, 10, 26, 10,  0,   0,  0,   13, 73, 72,  68,  82,  0,
                                     0,   0,   1,  0,   0,  0,  1,  8,   6,   0,  0,   0,  31, 21,  196, 137, 0,
                                     0,   0,   11, 73,  68, 65, 84, 120, 156, 99, 248, 15, 4,  0,   9,   251, 3,
                                     253, 251, 94, 107, 43, 0,  0,  0,   0,   73, 69,  78, 68, 174, 66,  96,  130};
    {
        std::ofstream stream(fixture.root / "test.png", std::ios::binary);
        stream.write(reinterpret_cast<const char*>(png), sizeof(png));
    }

    textures["textures"]["core:test"] = {{"file", "test.png"}};
    Write(texturesFile, textures);
    Reject([&] { return VoxelTextures::BuildAtlas(Take(load())); }, "expected a 16x16 PNG");

    const auto fixtures = std::filesystem::path(argv[1]).parent_path() / "tests/fixtures";

    CopyFile(fixtures / "opaque.png", fixture.root / "test.png");
    const auto pngRegistry = Take(load());
    const auto pngAtlas = Take(VoxelTextures::BuildAtlas(pngRegistry));
    size_t pngTile = 0;

    while (pngRegistry.Textures()[pngTile].key != "core:test") {
        ++pngTile;
    }

    const size_t pngWidth = pngRegistry.Textures().size() * 16;

    for (size_t y = 0; y < 16; ++y) {
        for (size_t x = 0; x < 16; ++x) {
            const auto offset = (y * pngWidth + pngTile * 16 + x) * 4;
            Require(pngAtlas[offset] == 20 && pngAtlas[offset + 1] == 40 && pngAtlas[offset + 2] == 60 &&
                        pngAtlas[offset + 3] == 255,
                    "PNG atlas pixels do not match source");
        }
    }

    CopyFile(fixtures / "transparent.png", fixture.root / "test.png");
    Reject([&] { return VoxelTextures::BuildAtlas(Take(load())); }, "only opaque textures");
    {
        std::ofstream stream(fixture.root / "test.png");
        stream << "not a PNG";
    }

    Reject([&] { return VoxelTextures::BuildAtlas(Take(load())); }, "test.png");

    // Exercise tiles above the old 8-bit boundary through the non-uniform meshing path.
    textures = validTextures;

    for (int i = 0; i < 300; ++i) {
        textures["textures"]["test:tile" + std::to_string(i)] = {{"color", {100, 100, 100}}};
    }

    textures["textures"]["zz:last"] = {{"color", {255, 0, 0}}};
    Write(texturesFile, textures);
    const Json added = {
        {"schema_version", 1}, {"id", "test:new_block"}, {"name", "New block"}, {"textures", {{"all", "zz:last"}}}};
    Write(fixture.root / "blocks/new.json", added);

    Reject([&] { return Blocks::Initialize(fixture.root / "missing"); }, "cannot open file");

    const auto initialized = Blocks::Initialize(fixture.root);
    Require(initialized.has_value(), "registry initialization failed");

    Reject([&] { return Blocks::Initialize(fixture.root); }, "already initialized");

    const auto& registry = Blocks::Registry();
    const auto newId = registry.Find("test:new_block");
    Require(newId.has_value(), "new block lookup failed");

    const auto tile = registry.Get(*newId).faceTiles[0];
    Require(newId == 4 && tile > 255, "additional block or high tile ID failed");
    Octree octree(2);
    octree.SetVoxel(1, 1, 1, *newId);
    const auto mesh = BuildChunkMesh(octree);
    Require(mesh.vertices.size() == 24 && mesh.indices.size() == 36, "new block did not mesh");

    for (const auto& vertex : mesh.vertices) {
        Require(vertex.tile == tile && vertex.occlusion == 1.0f, "tile packing corrupted texture or AO");
    }

    octree.Fill(Blocks::Grass);
    const auto uniformMesh = BuildChunkMesh(octree);
    Require(uniformMesh.vertices.size() == 24, "uniform chunk did not mesh");

    for (const auto& vertex : uniformMesh.vertices) {
        const auto expected = vertex.normal.y > 0   ? grass.faceTiles[3]
                              : vertex.normal.y < 0 ? grass.faceTiles[2]
                                                    : grass.faceTiles[0];
        Require(vertex.tile == expected, "uniform grass face texture changed");
    }

    std::cout << "Block registry, validation, atlas, and meshing checks passed.\n";
    return 0;
}
