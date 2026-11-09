#pragma once

#include "meshing/ChunkMesh.h"
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <cstddef>

namespace ShaderData {
    struct VoxelParameters {
        glm::mat4 viewProjection;
        glm::vec4 chunkOrigin;
        glm::vec4 cameraPositionFogStart;
        float fogEnd;
        uint32_t textureIndex;
        uint32_t samplerIndex;
        uint32_t vertexBufferIndex;
        uint32_t sunDataIndex;
    };

    struct alignas(16) SunData {
        glm::vec4 sunDirection;
        uint32_t shadowDataIndex;
        glm::vec3 padding; // std430 rounds the structure stride to 16-byte alignment.
    };
    static_assert(offsetof(SunData, shadowDataIndex) == 16);
    static_assert(sizeof(SunData) == 32); // Match the shader's std430 structure stride.

    struct SkyParameters {
        glm::mat4 inverseViewProjection;
        glm::vec4 sunDirectionSize; // Normalized direction and tangent of angular half-size.
        glm::vec4 sunRight; // Orbit axis keeps the disc orientation stable through noon.
    };

    struct DepthParameters { glm::mat4 viewProjection; glm::vec4 origin; uint32_t vertices; };
    static_assert(offsetof(VoxelParameters, fogEnd) == 96);
    static_assert(sizeof(VoxelParameters) == 116);
    static_assert(sizeof(SkyParameters) == 96);
    static_assert(offsetof(DepthParameters, vertices) == 80);
    static_assert(sizeof(DepthParameters) == 84);
    static_assert(sizeof(VoxelVertex) == 40);
    static_assert(offsetof(VoxelVertex, tile) == 32);
    static_assert(offsetof(VoxelVertex, occlusion) == 36);
}
