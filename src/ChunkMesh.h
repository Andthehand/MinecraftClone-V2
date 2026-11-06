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

inline void AddChunkQuad(ChunkMesh& mesh, glm::ivec3 cell, int axis, int sign,
    int width, int height, uint32_t tile) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    constexpr int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const auto& corner : corners) {
        VoxelVertex vertex{glm::vec3(cell), glm::vec3(0.0f), {}, tile};
        vertex.position[axis] += sign > 0 ? 1.0f : 0.0f;
        vertex.position[u] += float(corner[0] * width);
        vertex.position[v] += float(corner[1] * height);
        vertex.normal[axis] = float(sign);
        // UVs repeat once per block; side textures keep their grassy edge at +Y.
        if (axis == 0)
            vertex.uv = {float(corner[1] * height), float((1 - corner[0]) * width)};
        else if (axis == 2)
            vertex.uv = {float(corner[0] * width), float((1 - corner[1]) * height)};
        else
            vertex.uv = {float(corner[0] * width), float(corner[1] * height)};
        mesh.vertices.push_back(vertex);
    }
    if (sign > 0)
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    else
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

// Greedily merge coplanar faces with the same texture. Keep chunk boundaries
// closed even when neighbors are unloaded, just as for the voxel face mesh.
inline ChunkMesh BuildChunkMesh(const Octree& octree) {
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    if (octree.IsUniform()) {
        const uint32_t material = octree.GetVoxel(0, 0, 0);
        if (material != VoxelTextures::Air) {
            for (int axis = 0; axis < 3; ++axis)
                for (int sign : {-1, 1}) {
                    glm::ivec3 cell(0);
                    cell[axis] = sign > 0 ? size - 1 : 0;
                    AddChunkQuad(mesh, cell, axis, sign, size, size,
                        VoxelTextures::FaceTile(material, axis, sign));
                }
        }
        return mesh;
    }

    std::vector<uint32_t> mask(static_cast<size_t>(size) * size);
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int sign : {-1, 1}) {
            for (int slice = 0; slice < size; ++slice) {
                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size; ++i) {
                        glm::ivec3 cell(0);
                        cell[axis] = slice; cell[u] = i; cell[v] = j;
                        const auto material = octree.GetVoxel(cell.x, cell.y, cell.z);
                        auto neighbor = cell;
                        neighbor[axis] += sign;
                        const bool exposed = neighbor[axis] < 0 || neighbor[axis] >= size ||
                            octree.GetVoxel(neighbor.x, neighbor.y, neighbor.z) == VoxelTextures::Air;
                        mask[static_cast<size_t>(j) * size + i] =
                            material != VoxelTextures::Air && exposed
                            ? VoxelTextures::FaceTile(material, axis, sign) + 1 : 0;
                    }
                }
                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size;) {
                        const auto tile = mask[static_cast<size_t>(j) * size + i];
                        if (tile == 0) { ++i; continue; }
                        int width = 1;
                        while (i + width < size && mask[static_cast<size_t>(j) * size + i + width] == tile)
                            ++width;
                        int height = 1;
                        for (; j + height < size; ++height) {
                            bool matches = true;
                            for (int k = 0; k < width; ++k)
                                if (mask[static_cast<size_t>(j + height) * size + i + k] != tile) {
                                    matches = false;
                                    break;
                                }
                            if (!matches) break;
                        }
                        glm::ivec3 cell(0);
                        cell[axis] = slice; cell[u] = i; cell[v] = j;
                        AddChunkQuad(mesh, cell, axis, sign, width, height, tile - 1);
                        for (int row = 0; row < height; ++row)
                            for (int col = 0; col < width; ++col)
                                mask[static_cast<size_t>(j + row) * size + i + col] = 0;
                        i += width;
                    }
                }
            }
        }
    }
    return mesh;
}
