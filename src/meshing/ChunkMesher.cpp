#include "meshing/ChunkMesher.h"
#include "meshing/BlockFaces.h"
#include "diagnostics/Profiling.h"

#include <cstddef>
#include <cstdlib>

namespace {
void AddChunkQuad(ChunkMesh& mesh, glm::ivec3 cell, int axis, int sign, int width, int height, uint32_t tile,
                  uint32_t ao = 255u) {
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
        if (axis == 0) {
            vertex.uv = {float(corner[1] * height), float((1 - corner[0]) * width)};
        } else if (axis == 2) {
            vertex.uv = {float(corner[0] * width), float((1 - corner[1]) * height)};
        } else {
            vertex.uv = {float(corner[0] * width), float(corner[1] * height)};
        }

        mesh.vertices.push_back(vertex);
    }

    // Choose the diagonal that avoids a dark interpolation stripe.
    const bool flip = (ao & 3u) + ((ao >> 4) & 3u) > ((ao >> 2) & 3u) + ((ao >> 6) & 3u);
    const std::array<uint32_t, 6> indices =
        flip ? std::array<uint32_t, 6>{0, 1, 3, 1, 2, 3} : std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};

    for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        mesh.indices.push_back(base + indices[triangle]);
        mesh.indices.push_back(base + indices[triangle + (sign > 0 ? 1 : 2)]);
        mesh.indices.push_back(base + indices[triangle + (sign > 0 ? 2 : 1)]);
    }
}

// Reuse scratch storage on each meshing thread without retaining world pointers.
struct MeshScratch {
    std::vector<uint32_t> voxels;
    std::vector<uint8_t> solid;
    std::vector<uint32_t> mask;
};
} // namespace

ChunkMesh BuildChunkMesh(const Octree& octree, const ChunkMeshNeighbors& neighbors) {
    MC_PROFILE_ZONE("Build chunk mesh");
    ChunkMesh mesh;
    const int size = static_cast<int>(octree.GetSideLength());
    const size_t side = static_cast<size_t>(size);
    const bool uniform = octree.IsUniform();
    const uint32_t uniformMaterial = uniform ? octree.GetVoxel(0, 0, 0) : Blocks::Air;

    if (uniform && uniformMaterial == Blocks::Air) {
        return mesh;
    }

    bool exteriorEmpty = true;
    bool enclosed = true;

    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }

                const Octree* neighbor = neighbors[ChunkNeighborIndex(x, y, z)];
                const bool neighborUniform = neighbor && neighbor->IsUniform();
                const bool neighborSolid = neighborUniform && neighbor->GetVoxel(0, 0, 0) != Blocks::Air;
                exteriorEmpty &= !neighbor || (neighborUniform && !neighborSolid);

                if (std::abs(x) + std::abs(y) + std::abs(z) == 1) {
                    enclosed &= neighborSolid;
                }
            }
        }
    }

    if (uniform && enclosed) {
        return mesh;
    }

    if (uniform && exteriorEmpty) {
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);

        for (int axis = 0; axis < 3; ++axis) {
            for (int sign : {-1, 1}) {
                glm::ivec3 cell(0);
                cell[axis] = sign > 0 ? size - 1 : 0;
                AddChunkQuad(mesh, cell, axis, sign, size, size, BlockFaces::FaceTile(uniformMaterial, axis, sign));
            }
        }

        return mesh;
    }

    thread_local MeshScratch scratch;
    auto& voxels = scratch.voxels;
    auto& solid = scratch.solid;
    auto& mask = scratch.mask;
    // A one-voxel halo includes face, edge and corner neighbors for AO.
    const ptrdiff_t stride = size + 2;
    const ptrdiff_t plane = stride * stride;
    const auto indexOf = [=](int x, int y, int z) { return (z + 1) * plane + (y + 1) * stride + x + 1; };
    solid.assign(static_cast<size_t>(plane * stride), 0);

    if (!uniform) {
        voxels.resize(side * side * side);
        octree.CopyVoxels(voxels);
    }

    // Uniform chunks only visit boundary slices, whose visibility and AO read
    // the exterior halo. Their interior occupancy is never sampled.
    if (!uniform) {
        for (int z = 0; z < size; ++z) {
            for (int y = 0; y < size; ++y) {
                auto* row = solid.data() + indexOf(0, y, z);
                const auto* materials = voxels.data() + (z * side + y) * side;

                for (int x = 0; x < size; ++x) {
                    row[x] = materials[x] != Blocks::Air;
                }
            }
        }
    }

    // Only sample the touching slab/edge/corner of each neighboring octree.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) {
                    continue;
                }

                const Octree* neighbor = neighbors[ChunkNeighborIndex(dx, dy, dz)];

                if (!neighbor) {
                    continue;
                }

                const bool neighborUniform = neighbor->IsUniform();
                const bool neighborSolid = neighborUniform && neighbor->GetVoxel(0, 0, 0) != Blocks::Air;

                if (neighborUniform && !neighborSolid) {
                    continue;
                }

                const int xBegin = dx < 0 ? -1 : dx > 0 ? size : 0;
                const int yBegin = dy < 0 ? -1 : dy > 0 ? size : 0;
                const int zBegin = dz < 0 ? -1 : dz > 0 ? size : 0;
                const int xEnd = dx == 0 ? size : xBegin + 1;
                const int yEnd = dy == 0 ? size : yBegin + 1;
                const int zEnd = dz == 0 ? size : zBegin + 1;

                for (int z = zBegin; z < zEnd; ++z) {
                    for (int y = yBegin; y < yEnd; ++y) {
                        for (int x = xBegin; x < xEnd; ++x) {
                            solid[indexOf(x, y, z)] =
                                neighborUniform
                                    ? neighborSolid
                                    : neighbor->GetVoxel(x - dx * size, y - dy * size, z - dz * size) != Blocks::Air;
                        }
                    }
                }
            }
        }
    }

    const ptrdiff_t steps[3] = {1, stride, plane};
    const auto faceAO = [&](ptrdiff_t cell, int axis, int sign) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        cell += sign * steps[axis];
        constexpr int corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        uint32_t result = 0;

        for (int corner = 0; corner < 4; ++corner) {
            const auto du = corners[corner][0] * steps[u];
            const auto dv = corners[corner][1] * steps[v];
            const bool a = solid[cell + du], b = solid[cell + dv];
            const uint32_t level = a && b ? 0u : 3u - uint32_t(a) - uint32_t(b) - uint32_t(solid[cell + du + dv]);
            result |= level << (corner * 2);
        }

        return result;
    };
    mask.resize(side * side);

    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;

        for (int sign : {-1, 1}) {
            glm::ivec3 adjacentOffset(0);
            adjacentOffset[axis] = sign;
            const Octree* adjacent =
                neighbors[ChunkNeighborIndex(adjacentOffset.x, adjacentOffset.y, adjacentOffset.z)];

            if (uniform && adjacent && adjacent->IsUniform() && adjacent->GetVoxel(0, 0, 0) != Blocks::Air) {
                continue;
            }

            // Uniform solid chunks only have potentially visible boundary faces.
            const int firstSlice = uniform && sign > 0 ? size - 1 : 0;
            const int endSlice = uniform ? firstSlice + 1 : size;

            for (int slice = firstSlice; slice < endSlice; ++slice) {
                bool hasFaces = false;

                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size; ++i) {
                        glm::ivec3 cell(0);
                        cell[axis] = slice;
                        cell[u] = i;
                        cell[v] = j;
                        const auto material =
                            uniform ? uniformMaterial : voxels[(cell.z * side + cell.y) * side + cell.x];

                        if (material == Blocks::Air) {
                            mask[static_cast<size_t>(j) * size + i] = 0;
                            continue;
                        }

                        const auto cellIndex = indexOf(cell.x, cell.y, cell.z);
                        const bool exposed = !solid[cellIndex + sign * steps[axis]];
                        hasFaces |= exposed;
                        mask[static_cast<size_t>(j) * size + i] =
                            exposed ? (BlockFaces::FaceTile(material, axis, sign) + 1) |
                                          (faceAO(cellIndex, axis, sign) << 8)
                                    : 0;
                    }
                }

                if (!hasFaces) {
                    continue;
                }

                for (int j = 0; j < size; ++j) {
                    for (int i = 0; i < size;) {
                        const auto tile = mask[static_cast<size_t>(j) * size + i];

                        if (tile == 0) {
                            ++i;
                            continue;
                        }

                        const uint32_t ao = tile >> 8;
                        // Only constant AO can be merged without losing interior shading.
                        const bool mergeable = ao == (ao & 3u) * 85u;
                        int width = 1;

                        while (mergeable && i + width < size &&
                               mask[static_cast<size_t>(j) * size + i + width] == tile) {
                            ++width;
                        }

                        int height = 1;

                        for (; mergeable && j + height < size; ++height) {
                            bool matches = true;

                            for (int k = 0; k < width; ++k) {
                                if (mask[static_cast<size_t>(j + height) * size + i + k] != tile) {
                                    matches = false;
                                    break;
                                }
                            }

                            if (!matches) {
                                break;
                            }
                        }

                        glm::ivec3 cell(0);
                        cell[axis] = slice;
                        cell[u] = i;
                        cell[v] = j;
                        AddChunkQuad(mesh, cell, axis, sign, width, height, (tile & 255u) - 1, ao);

                        for (int row = 0; row < height; ++row) {
                            for (int col = 0; col < width; ++col) {
                                mask[static_cast<size_t>(j + row) * size + i + col] = 0;
                            }
                        }

                        i += width;
                    }
                }
            }
        }
    }

    return mesh;
}
