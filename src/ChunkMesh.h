#pragma once
#include "Octree.h"
#include "VoxelTextures.h"
#include <vector>
#include <cstddef>
#include <type_traits>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct VoxelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t tile;
};

struct ChunkMesh {
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t> indices;
};

// Keep chunk boundaries closed even when neighbors are unloaded.
// Emit a quad for each voxel face exposed to air.
inline ChunkMesh BuildChunkMesh(const Octree& octree) {
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const uint32_t material = octree.GetVoxel(x, y, z);
                if (material == VoxelTextures::Air) {
                    // Empty voxel, skip...
                    continue;
                }

                const glm::ivec3 cell(x, y, z);
                for (int axis = 0; axis < 3; ++axis) {
                    for (int sign : {-1, 1}) {
                        glm::ivec3 neighbor = cell;
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
                            VoxelVertex vertex{ glm::vec3(cell), glm::vec3(0.0f), {},
                                VoxelTextures::FaceTile(material, axis, sign) };
                            vertex.position[axis] += sign > 0 ? 1.0f : 0.0f;
                            vertex.position[u] += float(corner[0]);
                            vertex.position[v] += float(corner[1]);
                            vertex.normal[axis] = float(sign);
                            // Side textures always have their grassy edge at +Y.
                            if (axis == 0)
                                vertex.uv = {float(corner[1]), 1.0f - float(corner[0])};
                            else if (axis == 2)
                                vertex.uv = {float(corner[0]), 1.0f - float(corner[1])};
                            else
                                vertex.uv = {float(corner[0]), float(corner[1])};
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
