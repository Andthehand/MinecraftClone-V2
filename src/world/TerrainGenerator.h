#pragma once

#include "world/Octree.h"
#include "config/Settings.h"
#include <FastNoise/FastNoise.h>
#include <glm/vec3.hpp>

// A reusable noise graph; generation is deterministic in world space.
class TerrainGenerator {
public:
    explicit TerrainGenerator(int seed = Options::World::TERRAIN_SEED);

    // Coordinates are in chunks, with each chunk sized to the octree's side length.
    // Replaces every voxel, including air, so an existing octree can be regenerated.
    void Generate(Octree& octree, glm::ivec3 chunkCoord) const;
private:
    int m_Seed;
    FastNoise::SmartNode<> m_HeightNoise;
};
