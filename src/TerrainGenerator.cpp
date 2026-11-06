#include "TerrainGenerator.h"
#include "VoxelTextures.h"

#include <algorithm>
#include <cmath>
#include <vector>

TerrainGenerator::TerrainGenerator(int seed) : m_Seed(seed) {
    constexpr float gain = 0.5f;
    float amplitudeSum = 0.0f;
    float amplitude = 1.0f;
    for (int octave = 0; octave < Options::World::TERRAIN_OCTAVES; ++octave) {
        amplitudeSum += amplitude;
        amplitude *= gain;
    }
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    simplex->SetScale(Options::World::TERRAIN_FEATURE_SCALE);
    // FBm sums octave amplitudes; normalize the source to retain [-1, 1].
    simplex->SetOutputMin(-1.0f / amplitudeSum);
    simplex->SetOutputMax(1.0f / amplitudeSum);

    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(simplex);
    fractal->SetOctaveCount(Options::World::TERRAIN_OCTAVES);
    fractal->SetGain(gain);
    fractal->SetLacunarity(2.0f);
    m_HeightNoise = fractal;
}

void TerrainGenerator::Generate(Octree& octree, glm::ivec3 chunkCoord) const {
    const int size = static_cast<int>(octree.GetSideLength());
    const glm::ivec3 origin = chunkCoord * size;
    // Entire underground/sky chunks need only a single uniform node.
    constexpr int minSurface = Options::World::TERRAIN_BASE_HEIGHT - Options::World::TERRAIN_HEIGHT_AMPLITUDE;
    constexpr int maxSurface = Options::World::TERRAIN_BASE_HEIGHT + Options::World::TERRAIN_HEIGHT_AMPLITUDE;
    if (origin.y > maxSurface) {
        octree.Fill(VoxelTextures::Air);
        return;
    }
    if (origin.y + size - 1 < minSurface - Options::World::TERRAIN_DIRT_DEPTH) {
        octree.Fill(VoxelTextures::StoneBlock);
        return;
    }
    octree.Fill(VoxelTextures::Air);
    std::vector<float> heights(static_cast<size_t>(size) * size);
    // One SIMD batch per XZ column grid. Y never enters the height calculation,
    // so stacked chunks share the same surface and material layers.
    m_HeightNoise->GenUniformGrid2D(heights.data(), float(origin.x), float(origin.z),
        size, size, 1.0f, 1.0f, m_Seed);

    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const float noise = std::clamp(heights[static_cast<size_t>(z) * size + x], -1.0f, 1.0f);
            const int surface = static_cast<int>(std::floor(Options::World::TERRAIN_BASE_HEIGHT
                + noise * Options::World::TERRAIN_HEIGHT_AMPLITUDE));
            for (int y = 0; y < size; ++y) {
                const int worldY = origin.y + y;
                uint32_t material = VoxelTextures::Air;
                if (worldY == surface)
                    material = VoxelTextures::Grass;
                else if (worldY < surface)
                    material = surface - worldY <= Options::World::TERRAIN_DIRT_DEPTH
                        ? VoxelTextures::DirtBlock : VoxelTextures::StoneBlock;
                octree.SetVoxel(x, y, z, material);
            }
        }
    }
}
