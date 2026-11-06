#pragma once
#include "Octree.h"
#include "Options.h"
#include <Buffer.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <functional>
#include <unordered_map>

using ChunkCoord = glm::ivec3;

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const noexcept {
        size_t hash = std::hash<int32_t>{}(c.x);
        for (int32_t component : {c.y, c.z})
            hash ^= std::hash<int32_t>{}(component) + size_t{0x9e3779b9}
                + (hash << 6) + (hash >> 2);
        return hash;
    }
};

// Octrees remain CPU voxel storage; rendering uses an immutable surface mesh.
class World {
public:
    struct Chunk {
        Octree octree{Options::World::OCTREE_DEPTH};
        RealRHI::Ref<RealRHI::Buffer> vertices;
        RealRHI::Ref<RealRHI::Buffer> indices;
        uint32_t indexCount = 0;
    };
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks;

    // Floor before converting so negative positions map to the correct chunk.
	[[nodiscard]]
    static ChunkCoord ChunkCoordinate(glm::vec3 position) {
        return ChunkCoord(glm::floor(position / float(Options::World::CHUNK_SIZE)));
    }

	[[nodiscard]]
    static int Distance(ChunkCoord a, ChunkCoord b) {
        const auto delta = glm::abs(a - b);
        return glm::max(delta.x, glm::max(delta.y, delta.z));
    }

    // Preserve the sphere demo, repeated in world space to make streaming visible.
    static void Generate(Octree& octree) {
        constexpr float CENTER = Options::World::CHUNK_SIZE * 0.5f;
        constexpr float RADIUS = Options::World::CHUNK_SIZE * Options::World::SPHERE_RADIUS_RATIO;
        for (uint32_t z = 0; z < Options::World::CHUNK_SIZE; ++z)
            for (uint32_t y = 0; y < Options::World::CHUNK_SIZE; ++y)
                for (uint32_t x = 0; x < Options::World::CHUNK_SIZE; ++x) {
                    const glm::vec3 offset = glm::vec3(x, y, z) + glm::vec3(0.5f - CENTER);
                    if (glm::dot(offset, offset) <= RADIUS * RADIUS)
                        octree.SetVoxel(x, y, z, Options::World::SPHERE_VOXEL_ID);
                }
    }
};
