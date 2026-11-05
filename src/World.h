#pragma once
#include "Octree.h"
#include "Options.h"
#include "FreeListAllocator.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

struct ChunkCoord {
    int32_t x, y, z;
    bool operator==(const ChunkCoord&) const = default;
};

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const noexcept {
        size_t hash = std::hash<int32_t>{}(c.x);
        for (int32_t component : {c.y, c.z})
            hash ^= std::hash<int32_t>{}(component) + size_t{Options::Hashing::COMBINE_SEED}
                + (hash << Options::Hashing::LEFT_SHIFT) + (hash >> Options::Hashing::RIGHT_SHIFT);
        return hash;
    }
};

// Each chunk owns a local octree; GPU child indices remain relative to its allocation.
class World {
public:
    struct Chunk {
        Octree octree{Options::World::OCTREE_DEPTH};
        FreeListAllocator::Allocation allocation;
    };
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks;

    // Chunk coords are are not voxel coordinates
	[[nodiscard]]
    static ChunkCoord ChunkCoordinate(float x, float y, float z) {
        return {static_cast<int32_t>(std::floor(x / Options::World::CHUNK_SIZE)),
                static_cast<int32_t>(std::floor(y / Options::World::CHUNK_SIZE)),
                static_cast<int32_t>(std::floor(z / Options::World::CHUNK_SIZE))};
    }

	[[nodiscard]]
    static int Distance(ChunkCoord a, ChunkCoord b) {
        return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
    }

	// Voxel coordinates are relative to the chunk origin, not world origin.
	[[nodiscard]]
    static uint32_t VoxelIndex(int x, int y, int z) {
        return static_cast<uint32_t>(x + Options::World::GRID_SIZE * (y + Options::World::GRID_SIZE * z));
    }

    // Preserve the sphere demo, repeated in world space to make streaming visible.
    static void Generate(Octree& octree) {
        constexpr float CENTER = Options::World::CHUNK_SIZE * 0.5f;
        constexpr float RADIUS = Options::World::CHUNK_SIZE * Options::World::SPHERE_RADIUS_RATIO;
        for (uint32_t z = 0; z < Options::World::CHUNK_SIZE; ++z)
            for (uint32_t y = 0; y < Options::World::CHUNK_SIZE; ++y)
                for (uint32_t x = 0; x < Options::World::CHUNK_SIZE; ++x) {
                    const float dx = x + 0.5f - CENTER;
                    const float dy = y + 0.5f - CENTER;
                    const float dz = z + 0.5f - CENTER;
                    if (dx * dx + dy * dy + dz * dz <= RADIUS * RADIUS)
                        octree.SetVoxel(x, y, z, Options::World::SPHERE_VOXEL_ID);
                }
    }
};
