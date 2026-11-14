#include "world/ChunkStreamer.h"
#include "meshing/ChunkMesher.h"
#include "diagnostics/Profiling.h"
#include <algorithm>

namespace {
// Returns a list of chunk offsets to load, sorted by distance from the origin.
const std::vector<ChunkCoord>& GetChunkLoadOffsets() {
    static const std::vector<ChunkCoord> offsets = [] {
        std::vector<ChunkCoord> result;
        constexpr int radius = Options::World::LOAD_RADIUS_CHUNKS;

        for (int z = -radius; z <= radius; ++z) {
            for (int y = -radius; y <= radius; ++y) {
                for (int x = -radius; x <= radius; ++x) {
                    result.push_back({x, y, z});
                }
            }
        }

        std::stable_sort(result.begin(), result.end(), [](ChunkCoord a, ChunkCoord b) {
            return World::Distance(a, {0, 0, 0}) < World::Distance(b, {0, 0, 0});
        });

        return result;
    }();

    return offsets;
}

} // namespace

ChunkUpdates ChunkStreamer::Update(World& world, glm::vec3 position) {
    MC_PROFILE_ZONE("Stream chunks");
    ChunkUpdates updates;
    const ChunkCoord player = World::ChunkCoordinate(position);

    for (const auto& [coord, chunk] : world.GetChunks()) {
        if (World::Distance(coord, player) > Options::World::UNLOAD_RADIUS_CHUNKS) {
            updates.removed.push_back(coord);
        }
    }

    for (const auto coord : updates.removed) {
        world.RemoveChunk(coord);
    }

    uint32_t loaded = 0;

    for (const auto offset : GetChunkLoadOffsets()) {
        if (loaded == Options::World::MAX_CHUNKS_GENERATED_PER_FRAME) {
            break;
        }

        const ChunkCoord coord = player + offset;

        if (world.ContainsChunk(coord)) {
            continue;
        }

        world.GenerateChunk(coord);
        ++loaded;
    }

    // Keep the voxel-only border and require all 26 neighbors, including AO corners.
    for (const auto& [coord, chunk] : world.GetChunks()) {
        if (!chunk.meshDirty || !world.CanMeshChunk(coord, player)) {
            continue;
        }

        updates.completed.push_back({coord, BuildChunkMesh(chunk.octree, world.GetChunkNeighbors(coord))});
        world.ClearMeshDirtyFlag(coord);
    }

    MC_PROFILE_PLOT("World chunks", static_cast<int64_t>(world.GetChunks().size()));
    MC_PROFILE_PLOT("Generated chunks", static_cast<int64_t>(loaded));

    return updates;
}
