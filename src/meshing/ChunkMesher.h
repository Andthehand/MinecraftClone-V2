#pragma once

#include "meshing/ChunkMesh.h"

// Greedily merge faces with constant, matching AO and texture. Faces with AO
// gradients stay block-sized. Neighbor octrees must have the same side length.
// Scratch voxel/occupancy/mask buffers are reused per thread (about 1.3 MiB for
// 64^3 chunks); returned meshes own their vertices and indices independently.
ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors = {});
