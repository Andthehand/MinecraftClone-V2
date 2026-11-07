#pragma once

#include "ChunkMesh.h"
#include <cstddef>
#include <span>
#include <vector>

namespace VoxelShadows {
	// Creates a 3x3x3 voxel occupancy volume for ray-traced sun shadows.
	// Stores a header followed by a bitfield of solid voxels, then a bitfield of solid bricks i.e. BRICK_SIZExBRICK_SIZExBRICK_SIZE.
    class VoxelShadowVolume {
    public:
		// minimumVoxel is the world coordinate of the volume's (0,0,0) corner. The volume is always 3 chunks wide.
        // Uses ChunkMeshNeighbors indexing, including the center chunk.
        static VoxelShadowVolume Build(const ChunkMeshNeighbors& chunks, glm::ivec3 minimumVoxel);

        std::span<const std::byte> GetUploadData() const;
    private:
        explicit VoxelShadowVolume(glm::ivec3 minimumVoxel);
        void PopulateFromChunk(const Octree& chunk, glm::uvec3 chunkOrigin, std::span<uint32_t> chunkVoxels);

        // Position is relative to the volume origin, not a world coordinate.
        void SetVoxelOccupied(glm::uvec3 position);
    private:
        std::vector<uint32_t> m_Packed;
    };

}
