#pragma once
#include "Octree.h"
#include <vector>

struct VoxelVertex { 
    float position[3]; 
    float normal[3]; 
};
struct ChunkMesh { 
    std::vector<VoxelVertex> vertices; 
    std::vector<uint32_t> indices; 
};

// Keep chunk boundaries closed even when neighbors are unloaded.
// This is a simple greedy meshing algorithm that generates a mesh for each voxel face that is exposed to air.
inline ChunkMesh BuildChunkMesh(const Octree& octree) {
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (octree.GetVoxel(x, y, z) == 0) {
                    // Empty voxel, skip...
                    continue;
                }

                const int cell[3] = { x, y, z };
                for (int axis = 0; axis < 3; ++axis) {
                    for (int sign : {-1, 1}) {
                        int neighbor[3] = { x, y, z };
                        neighbor[axis] += sign;
                        if (neighbor[axis] >= 0 && neighbor[axis] < size &&
                            octree.GetVoxel(neighbor[0], neighbor[1], neighbor[2]) != 0) {
                            // Neighboring voxel is solid, skip this face...
                            continue;
                        }

                        const auto base = static_cast<uint32_t>(mesh.vertices.size());
                        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                        constexpr int corners[4][2] = { { 0, 0 },{ 1, 0 },{ 1, 1 },{ 0, 1 } };
                        for (const auto& corner : corners) {
                            VoxelVertex vertex{ { float(cell[0]), float(cell[1]), float(cell[2]) },{} };
                            vertex.position[axis] += sign > 0 ? 1.0f : 0.0f;
                            vertex.position[u] += float(corner[0]);
                            vertex.position[v] += float(corner[1]);
                            vertex.normal[axis] = float(sign);
                            mesh.vertices.push_back(vertex);
                        }

                        // Add two triangles for the face, winding order depends on the sign of the normal.
                        if (sign > 0) {
                            mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
                        }
                        else {
                            mesh.indices.insert(mesh.indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
                        }
                    }
                }
            }
        }
    }

    return mesh;
}
