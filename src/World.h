#pragma once
#include "Octree.h"
#include "ChunkMesh.h"
#include "Options.h"
#include "TerrainGenerator.h"
#include <Buffer.h>

#include <glm/vec3.hpp>
#include <cstddef>
#include <unordered_map>

using ChunkCoord = glm::ivec3;

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const noexcept;
};

// Octrees remain CPU voxel storage; rendering uses an immutable surface mesh.
class World {
public:
    [[nodiscard]]
    ChunkMeshNeighbors GetChunkNeighbors(ChunkCoord coord) const;

    [[nodiscard]]
    bool CanMeshChunk(ChunkCoord coord, ChunkCoord player) const;

    // Floor before converting so negative positions map to the correct chunk.
	[[nodiscard]]
    static ChunkCoord ChunkCoordinate(glm::vec3 position);

	[[nodiscard]]
    static int Distance(ChunkCoord a, ChunkCoord b);

    void Generate(Octree& octree, ChunkCoord coord) const;
public:
    struct Chunk {
        Octree octree{ Options::World::CHUNK_OCTREE_DEPTH };
        RealRHI::Ref<RealRHI::Buffer> vertices;
        RealRHI::Ref<RealRHI::Buffer> indices;
        uint32_t indexCount = 0;
        bool meshDirty = true;
    };

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks;

private:
    TerrainGenerator m_Terrain;
};
