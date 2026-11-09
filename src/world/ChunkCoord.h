#pragma once

#include <glm/vec3.hpp>
#include <cstddef>

using ChunkCoord = glm::ivec3;

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const noexcept;
};
