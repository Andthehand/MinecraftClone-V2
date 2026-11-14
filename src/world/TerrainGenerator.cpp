#include "world/TerrainGenerator.h"
#include "world/Block.h"
#include "diagnostics/Profiling.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {
struct HeightRange {
    int min;
    int max;
};

// A quadtree over the XZ height map. Both vertical octants share these
// bounds, so each footprint is measured only once.
HeightRange BuildHeightRanges(std::vector<HeightRange>& ranges, size_t index, const std::vector<float>& heights,
                              int stride, int x, int z, int size) {
    if (size == 1) {
        const float noise = std::clamp(heights[static_cast<size_t>(z) * stride + x], -1.0f, 1.0f);
        const int surface = static_cast<int>(
            std::floor(Options::World::TERRAIN_BASE_HEIGHT + noise * Options::World::TERRAIN_HEIGHT_AMPLITUDE));

        return ranges[index] = {surface, surface};
    }

    const int half = size / 2;
    HeightRange bounds{};

    for (int child = 0; child < 4; ++child) {
        const auto range = BuildHeightRanges(ranges, index * 4 + 1 + child, heights, stride, x + (child & 1) * half,
                                             z + (child >> 1) * half, half);

        if (child == 0) {
            bounds = range;
        } else {
            bounds.min = std::min(bounds.min, range.min);
            bounds.max = std::max(bounds.max, range.max);
        }
    }

    return ranges[index] = bounds;
}

void BuildTerrainNode(Node& node, const std::vector<HeightRange>& ranges, size_t index, int bottom, int size) {
    const auto [min, max] = ranges[index];
    const int top = bottom + size - 1;
    constexpr int dirtDepth = Options::World::TERRAIN_DIRT_DEPTH;

    if (bottom > max) {
        node.voxel = Blocks::Air;
    } else if (top < min - dirtDepth) {
        node.voxel = Blocks::Stone;
    } else if (top < min && bottom >= max - dirtDepth) {
        node.voxel = Blocks::Dirt;
    } else if (size == 1) {
        node.voxel = Blocks::Grass;
    } else {
        node.children = std::make_unique<std::array<Node, 8>>();
        const int half = size / 2;

        for (int child = 0; child < 8; ++child) {
            const size_t footprint = index * 4 + 1 + (child & 1) + ((child >> 1) & 2);
            BuildTerrainNode((*node.children)[child], ranges, footprint, bottom + ((child >> 1) & 1) * half, half);
        }
    }
}
} // namespace

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
    MC_PROFILE_ZONE("Generate terrain chunk");
    const int size = static_cast<int>(octree.GetSideLength());
    const glm::ivec3 origin = chunkCoord * size;
    // Entire underground/sky chunks need only a single uniform node.
    constexpr int minSurface = Options::World::TERRAIN_BASE_HEIGHT - Options::World::TERRAIN_HEIGHT_AMPLITUDE;
    constexpr int maxSurface = Options::World::TERRAIN_BASE_HEIGHT + Options::World::TERRAIN_HEIGHT_AMPLITUDE;

    if (origin.y > maxSurface) {
        octree.Fill(Blocks::Air);

        return;
    }

    if (origin.y + size - 1 < minSurface - Options::World::TERRAIN_DIRT_DEPTH) {
        octree.Fill(Blocks::Stone);

        return;
    }

    std::vector<float> heights(static_cast<size_t>(size) * size);
    // One SIMD batch per XZ column grid. Y never enters the height calculation,
    // so stacked chunks share the same surface and material layers.
    {
        MC_PROFILE_ZONE("Generate height noise");
        m_HeightNoise->GenUniformGrid2D(heights.data(), float(origin.x), float(origin.z), size, size, 1.0f, 1.0f,
                                        m_Seed);
    }

    // A complete quadtree has (4 * leafCount - 1) / 3 nodes.
    std::vector<HeightRange> ranges((4 * heights.size() - 1) / 3);
    {
        MC_PROFILE_ZONE("Build height ranges");
        BuildHeightRanges(ranges, 0, heights, size, 0, 0, size);
    }

    Node root;
    {
        MC_PROFILE_ZONE("Build terrain octree");
        BuildTerrainNode(root, ranges, 0, origin.y, size);
    }

    octree.m_Root = std::move(root);
}
