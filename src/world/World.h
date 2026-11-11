#pragma once

#include "world/Chunk.h"
#include "world/ChunkCoord.h"
#include "world/ChunkNeighborhood.h"
#include "world/TerrainGenerator.h"

#include <unordered_map>
#include <optional>

struct BlockHit {
    glm::ivec3 position;
    glm::ivec3 normal; // Face pointing toward the ray origin; zero when starting inside solid.
    float distance;
};

class World {
public:
    using ChunkMap = std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>;

    // Getters
    [[nodiscard]] const ChunkMap& GetChunks() const { return m_Chunks; }
    [[nodiscard]] uint64_t GetNavigationRevision(ChunkCoord coord) const;
    [[nodiscard]] uint64_t GetNavigationRevision() const { return m_NavigationRevision; }
    [[nodiscard]] ChunkMeshNeighbors GetChunkNeighbors(ChunkCoord coord) const;
    [[nodiscard]] std::optional<uint32_t> GetBlock(glm::ivec3 position) const;
    // Stops at unloaded terrain. Direction need not be normalized; reach is in blocks.
    [[nodiscard]] std::optional<BlockHit> Raycast(glm::vec3 origin, glm::vec3 direction, float reach) const;

    // Unloaded terrain is unknown, never implicitly air. Coordinates are blocks.
    // Edits loaded chunks only; also dirties neighboring meshes at boundaries.
    bool SetBlock(glm::ivec3 position, uint32_t block);

    [[nodiscard]] bool ContainsChunk(ChunkCoord coord) const { return m_Chunks.contains(coord); }
    [[nodiscard]] bool CanMeshChunk(ChunkCoord coord, ChunkCoord player) const;

    void ClearMeshDirtyFlag(ChunkCoord coord);
    void GenerateChunk(ChunkCoord coord);
    void RemoveChunk(ChunkCoord coord);

    // Floor before converting so negative positions map to the correct chunk.
    [[nodiscard]] static ChunkCoord ChunkCoordinate(glm::vec3 position);
    [[nodiscard]] static int Distance(ChunkCoord a, ChunkCoord b);
private:
    void MarkAffectedMeshesDirty(ChunkCoord coord, glm::ivec3 local);
private:
    ChunkMap m_Chunks;
    TerrainGenerator m_Terrain;

	// Used in cache to check if neighbor chunks have
    // changed since the last navigation graph build.
    uint64_t m_NavigationRevision = 0;
};
