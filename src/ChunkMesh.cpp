#include "ChunkMesh.h"
#include "VoxelTextures.h"

#include <cstddef>

namespace {
void AddChunkQuad(ChunkMesh& mesh, glm::ivec3 cell, int axis, int sign,
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
} // namespace

ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors) {
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    const bool uniform = octree.IsUniform();
    const uint32_t uniformMaterial = uniform ? octree.GetVoxel(0, 0, 0) : VoxelTextures::Air;
    if (uniform && uniformMaterial == VoxelTextures::Air) return mesh;

    // Traverse the tree once and reuse contiguous voxel reads for all six face
    // passes, instead of starting at the root for every cell and neighbor.
    const size_t side = static_cast<size_t>(size);
    std::vector<uint32_t> voxels;
    if (!uniform) {
        voxels.resize(side * side * side);
        octree.CopyVoxels(voxels);
    }
    const auto voxelAt = [&](glm::ivec3 cell) {
        if (uniform) return uniformMaterial;
        return voxels[(static_cast<size_t>(cell.z) * side + cell.y) * side + cell.x];
    };
    std::vector<uint32_t> mask(side * side);
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int sign : {-1, 1}) {
            const Octree* adjacent = neighbors[axis * 2 + (sign > 0 ? 1 : 0)];
            if (uniform && (!adjacent || adjacent->IsUniform())) {
                if (!adjacent || adjacent->GetVoxel(0, 0, 0) == VoxelTextures::Air) {
                    glm::ivec3 cell(0);
                    cell[axis] = sign > 0 ? size - 1 : 0;
                    AddChunkQuad(mesh, cell, axis, sign, size, size,
                        VoxelTextures::FaceTile(uniformMaterial, axis, sign));
                }
                continue;
            }
            // Uniform solid chunks only have potentially visible boundary faces.
            const int firstSlice = uniform && sign > 0 ? size - 1 : 0;
            const int endSlice = uniform ? firstSlice + 1 : size;
            for (int slice = firstSlice; slice < endSlice; ++slice) {
                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size; ++i) {
                        glm::ivec3 cell(0);
                        cell[axis] = slice; cell[u] = i; cell[v] = j;
                        const auto material = voxelAt(cell);
                        if (material == VoxelTextures::Air) {
                            mask[static_cast<size_t>(j) * size + i] = 0;
                            continue;
                        }
                        auto neighbor = cell;
                        neighbor[axis] += sign;
                        bool exposed;
                        if (neighbor[axis] < 0 || neighbor[axis] >= size) {
                            neighbor[axis] = sign > 0 ? 0 : size - 1;
                            exposed = !adjacent || adjacent->GetVoxel(neighbor.x, neighbor.y, neighbor.z)
                                == VoxelTextures::Air;
                        } else {
                            exposed = voxelAt(neighbor) == VoxelTextures::Air;
                        }
                        mask[static_cast<size_t>(j) * size + i] =
                            exposed
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
