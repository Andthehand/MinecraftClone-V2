#pragma once

#include "meshing/ChunkMesh.h"

// Greedily merge faces with constant, matching AO and texture. Faces with AO
// gradients stay block-sized. Neighbor octrees must have the same side length.
ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors = {});
