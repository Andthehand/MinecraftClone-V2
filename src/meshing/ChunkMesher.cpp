#include "meshing/ChunkMesh.h"
#include "meshing/BlockFaces.h"
#include "diagnostics/Profiling.h"

#include <cstddef>

namespace {
void AddChunkQuad(ChunkMesh& mesh, glm::ivec3 cell, int axis, int sign,
    int width, int height, uint32_t tile, uint32_t ao = 255u) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    constexpr int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int index = 0; index < 4; ++index) {
        const auto& corner = corners[index];
        VoxelVertex vertex{glm::vec3(cell), glm::vec3(0.0f), {}, tile};
        vertex.occlusion = 0.55f + 0.15f * float((ao >> (index * 2)) & 3u);
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
    // Choose the diagonal that avoids a dark interpolation stripe.
    const bool flip = (ao & 3u) + ((ao >> 4) & 3u) >
        ((ao >> 2) & 3u) + ((ao >> 6) & 3u);
    const std::array<uint32_t, 6> indices = flip
        ? std::array<uint32_t, 6>{0, 1, 3, 1, 2, 3}
        : std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};
    for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        mesh.indices.push_back(base + indices[triangle]);
        mesh.indices.push_back(base + indices[triangle + (sign > 0 ? 1 : 2)]);
        mesh.indices.push_back(base + indices[triangle + (sign > 0 ? 2 : 1)]);
    }
}
} // namespace

ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors) {
    MC_PROFILE_ZONE("Build chunk mesh");
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    const bool uniform = octree.IsUniform();
    const uint32_t uniformMaterial = uniform ? octree.GetVoxel(0, 0, 0) : Blocks::Air;
    if (uniform && uniformMaterial == Blocks::Air) return mesh;

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
    const auto solidAt = [&](glm::ivec3 cell) {
        glm::ivec3 offset(0);
        for (int axis = 0; axis < 3; ++axis) {
            if (cell[axis] < 0) { offset[axis] = -1; cell[axis] += size; }
            else if (cell[axis] >= size) { offset[axis] = 1; cell[axis] -= size; }
        }
        if (offset == glm::ivec3(0)) return voxelAt(cell) != Blocks::Air;
        const Octree* neighbor = neighbors[ChunkNeighborIndex(offset.x, offset.y, offset.z)];
        return neighbor && neighbor->GetVoxel(cell.x, cell.y, cell.z) != Blocks::Air;
    };
    const auto faceAO = [&](glm::ivec3 cell, int axis, int sign) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        cell[axis] += sign;
        constexpr int corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        uint32_t result = 0;
        for (int corner = 0; corner < 4; ++corner) {
            auto sideU = cell, sideV = cell, diagonal = cell;
            sideU[u] += corners[corner][0];
            sideV[v] += corners[corner][1];
            diagonal[u] += corners[corner][0];
            diagonal[v] += corners[corner][1];
            const bool a = solidAt(sideU), b = solidAt(sideV);
            const uint32_t level = a && b ? 0u : 3u - uint32_t(a) - uint32_t(b) - uint32_t(solidAt(diagonal));
            result |= level << (corner * 2);
        }
        return result;
    };
    // An isolated uniform chunk has no exterior occluders and needs just six quads.
    bool exteriorEmpty = true;
    for (const Octree* neighbor : neighbors)
        if (neighbor && (!neighbor->IsUniform() || neighbor->GetVoxel(0, 0, 0) != Blocks::Air))
            exteriorEmpty = false;
    std::vector<uint32_t> mask(side * side);
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int sign : {-1, 1}) {
            glm::ivec3 adjacentOffset(0);
            adjacentOffset[axis] = sign;
            const Octree* adjacent = neighbors[ChunkNeighborIndex(adjacentOffset.x, adjacentOffset.y, adjacentOffset.z)];
            if (uniform && adjacent && adjacent->IsUniform() && adjacent->GetVoxel(0, 0, 0) != Blocks::Air)
                continue;
            if (uniform && exteriorEmpty) {
                glm::ivec3 cell(0);
                cell[axis] = sign > 0 ? size - 1 : 0;
                AddChunkQuad(mesh, cell, axis, sign, size, size,
                    BlockFaces::FaceTile(uniformMaterial, axis, sign));
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
                        if (material == Blocks::Air) {
                            mask[static_cast<size_t>(j) * size + i] = 0;
                            continue;
                        }
                        auto neighbor = cell;
                        neighbor[axis] += sign;
                        bool exposed;
                        if (neighbor[axis] < 0 || neighbor[axis] >= size) {
                            neighbor[axis] = sign > 0 ? 0 : size - 1;
                            exposed = !adjacent || adjacent->GetVoxel(neighbor.x, neighbor.y, neighbor.z)
                                == Blocks::Air;
                        } else {
                            exposed = voxelAt(neighbor) == Blocks::Air;
                        }
                        mask[static_cast<size_t>(j) * size + i] =
                            exposed
                            ? (BlockFaces::FaceTile(material, axis, sign) + 1) | (faceAO(cell, axis, sign) << 8) : 0;
                    }
                }
                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size;) {
                        const auto tile = mask[static_cast<size_t>(j) * size + i];
                        if (tile == 0) { ++i; continue; }
                        const uint32_t ao = tile >> 8;
                        // Only constant AO can be merged without losing interior shading.
                        const bool mergeable = ao == (ao & 3u) * 85u;
                        int width = 1;
                        while (mergeable && i + width < size && mask[static_cast<size_t>(j) * size + i + width] == tile)
                            ++width;
                        int height = 1;
                        for (; mergeable && j + height < size; ++height) {
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
                        AddChunkQuad(mesh, cell, axis, sign, width, height, (tile & 255u) - 1, ao);
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
