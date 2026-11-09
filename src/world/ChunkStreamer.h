#pragma once

#include "world/World.h"
#include "meshing/ChunkMesh.h"
#include <vector>

struct CompletedChunkMesh {
    ChunkCoord coord;
    ChunkMesh mesh;
};

struct ChunkUpdates {
    std::vector<ChunkCoord> removed;
    std::vector<CompletedChunkMesh> completed;
};

// Synchronous streaming and CPU meshing; owns no graphics resources.
class ChunkStreamer {
public:
    [[nodiscard]] ChunkUpdates Update(World& world, glm::vec3 position);
};
