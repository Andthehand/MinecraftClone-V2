#include "VoxelShadowVolume.h"
#include "VoxelTextures.h"
#include "Options.h"

#include <cassert>
#include <cstring>
#include <glm/glm.hpp>
#include <span>

namespace {
    constexpr uint32_t CHUNKS_PER_SIDE = Options::Rendering::VOXEL_SHADOW_CHUNKS_PER_SIDE;
    constexpr uint32_t VOXELS_PER_SIDE = CHUNKS_PER_SIDE * Options::World::CHUNK_SIZE;
    constexpr uint32_t BRICK_SIZE = Options::Rendering::VOXEL_SHADOW_BRICK_SIZE;
    constexpr uint32_t BRICKS_PER_SIDE = VOXELS_PER_SIDE / BRICK_SIZE;

    // Byte layout consumed by voxel_shadow.slang. Scalar coordinates avoid GLM alignment changes.
    struct VoxelShadowHeader {
        int32_t minimumX;
        int32_t minimumY;
        int32_t minimumZ;
        uint32_t voxelsPerSide;
        uint32_t brickSize;
        uint32_t voxelOccupancyByteOffset;
        uint32_t brickOccupancyByteOffset;
        uint32_t bricksPerSide;
        uint32_t reserved[8]{};
    };
    constexpr uint32_t HEADER_WORD_COUNT = sizeof(VoxelShadowHeader) / sizeof(uint32_t);
    constexpr uint32_t VOXEL_OCCUPANCY_WORD_COUNT = (VOXELS_PER_SIDE * VOXELS_PER_SIDE * VOXELS_PER_SIDE + 31) / 32;
    constexpr uint32_t BRICK_OCCUPANCY_WORD_COUNT = (BRICKS_PER_SIDE * BRICKS_PER_SIDE * BRICKS_PER_SIDE + 31) / 32;

    // X varies fastest, matching Octree::CopyVoxels and the shader's grid lookup.
    uint32_t GridIndex(glm::uvec3 position, uint32_t cellsPerSide) {
        return (position.z * cellsPerSide + position.y) * cellsPerSide + position.x;
    }

    void SetOccupancyBit(std::span<uint32_t> occupancy, uint32_t index) {
        constexpr uint32_t bitsPerWord = 32;
        const uint32_t wordIndex = index / bitsPerWord;
        const uint32_t bitIndex = index % bitsPerWord;
        occupancy[wordIndex] |= 1u << bitIndex;
    }

}

void VoxelShadows::VoxelShadowVolume::PopulateFromChunk(const Octree& chunk, glm::uvec3 chunkOrigin,
    std::span<uint32_t> chunkVoxels) {
    if (chunk.IsUniform() && chunk.GetVoxel(0, 0, 0) == VoxelTextures::Air) return;

    chunk.CopyVoxels(chunkVoxels);
    constexpr uint32_t chunkSide = Options::World::CHUNK_SIZE;
    for (uint32_t z = 0; z < chunkSide; ++z) {
        for (uint32_t y = 0; y < chunkSide; ++y) {
            for (uint32_t x = 0; x < chunkSide; ++x) {
                const glm::uvec3 localPosition(x, y, z);
                if (chunkVoxels[GridIndex(localPosition, chunkSide)] == VoxelTextures::Air) {
                    continue;
                }

                SetVoxelOccupied(chunkOrigin + localPosition);
            }
        }
    }
}

VoxelShadows::VoxelShadowVolume::VoxelShadowVolume(glm::ivec3 minimumVoxel)
    : m_Packed(HEADER_WORD_COUNT + VOXEL_OCCUPANCY_WORD_COUNT + BRICK_OCCUPANCY_WORD_COUNT, 0) {
    const VoxelShadowHeader header{
        .minimumX = minimumVoxel.x,
        .minimumY = minimumVoxel.y,
        .minimumZ = minimumVoxel.z,
        .voxelsPerSide = VOXELS_PER_SIDE,
        .brickSize = BRICK_SIZE,
        .voxelOccupancyByteOffset = HEADER_WORD_COUNT * sizeof(uint32_t),
        .brickOccupancyByteOffset = (HEADER_WORD_COUNT + VOXEL_OCCUPANCY_WORD_COUNT) * sizeof(uint32_t),
        .bricksPerSide = BRICKS_PER_SIDE,
    };

    std::memcpy(m_Packed.data(), &header, sizeof(header));
}

void VoxelShadows::VoxelShadowVolume::SetVoxelOccupied(glm::uvec3 position) {
    const auto words = std::span<uint32_t>(m_Packed);
    SetOccupancyBit(words.subspan(HEADER_WORD_COUNT, VOXEL_OCCUPANCY_WORD_COUNT), GridIndex(position, VOXELS_PER_SIDE));

    // A brick is occupied if any voxel within it is solid.
    const glm::uvec3 brickPosition = position / BRICK_SIZE;
    SetOccupancyBit(words.subspan(HEADER_WORD_COUNT + VOXEL_OCCUPANCY_WORD_COUNT, BRICK_OCCUPANCY_WORD_COUNT), GridIndex(brickPosition, BRICKS_PER_SIDE));
}

std::span<const std::byte> VoxelShadows::VoxelShadowVolume::GetUploadData() const {
    return std::as_bytes(std::span<const uint32_t>(m_Packed));
}

VoxelShadows::VoxelShadowVolume VoxelShadows::VoxelShadowVolume::Build(const ChunkMeshNeighbors& chunks, glm::ivec3 minimumVoxel) {
    VoxelShadowVolume volume(minimumVoxel);
    constexpr uint32_t chunkSide = Options::World::CHUNK_SIZE;

    // Reuse one temporary chunk array while filling both occupancy grids.
    std::vector<uint32_t> chunkVoxels(chunkSide * chunkSide * chunkSide);
    for (uint32_t chunkZ = 0; chunkZ < CHUNKS_PER_SIDE; ++chunkZ) {
        for (uint32_t chunkY = 0; chunkY < CHUNKS_PER_SIDE; ++chunkY) {
            for (uint32_t chunkX = 0; chunkX < CHUNKS_PER_SIDE; ++chunkX) {
                // Neighbor coordinates are -1..1; volume chunk coordinates are 0..2.
                const auto neighborIndex = ChunkNeighborIndex(int(chunkX) - 1, int(chunkY) - 1, int(chunkZ) - 1);
                const Octree& chunk = *chunks[neighborIndex];
                const glm::uvec3 chunkOrigin = glm::uvec3(chunkX, chunkY, chunkZ) * chunkSide;
                
                volume.PopulateFromChunk(chunk, chunkOrigin, chunkVoxels);
            }
        }
    }
    return volume;
}
