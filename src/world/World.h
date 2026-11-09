#pragma once

#include "world/Chunk.h"
#include "world/ChunkCoord.h"
#include "world/ChunkNeighborhood.h"
#include "world/TerrainGenerator.h"
#include <unordered_map>

class World {
public:
    using ChunkMap = std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>;

    [[nodiscard]] const ChunkMap& GetChunks() const { return m_Chunks; }
    [[nodiscard]] bool Contains(ChunkCoord coord) const { return m_Chunks.contains(coord); }
    void GenerateChunk(ChunkCoord coord);
    void RemoveChunk(ChunkCoord coord);
    void MarkMeshed(ChunkCoord coord);

    [[nodiscard]] ChunkMeshNeighbors GetChunkNeighbors(ChunkCoord coord) const;
    [[nodiscard]] bool CanMeshChunk(ChunkCoord coord, ChunkCoord player) const;
    // Floor before converting so negative positions map to the correct chunk.
    [[nodiscard]] static ChunkCoord ChunkCoordinate(glm::vec3 position);
    [[nodiscard]] static int Distance(ChunkCoord a, ChunkCoord b);

private:
    ChunkMap m_Chunks;
    TerrainGenerator m_Terrain;
};
