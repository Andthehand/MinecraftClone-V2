#pragma once

#include "Options.h"
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

inline glm::vec3 SunOrbitAxis() {
    const glm::vec3 axis = glm::cross(glm::normalize(Options::Rendering::SUN_DIRECTION), glm::vec3(0, 1, 0));
    return glm::dot(axis, axis) > 0.0001f ? glm::normalize(axis) : glm::vec3(1, 0, 0);
}

// A camera-centered orthographic sun view. Snap its lateral position to whole
// shadow texels so camera movement doesn't make stationary shadow edges crawl.
inline glm::mat4 ShadowViewProjection(glm::vec3 cameraPosition, glm::vec3 sunDirection) {
    const glm::vec3 sun = glm::normalize(sunDirection);
    // Use the orbit axis as up so the light camera stays stable through noon.
    auto view = glm::mat4(glm::mat3(glm::lookAtRH(sun, glm::vec3(0), SunOrbitAxis())));
    const glm::vec3 center = glm::vec3(view * glm::vec4(cameraPosition, 1));
    constexpr float radius = Options::Rendering::SHADOW_RADIUS;
    constexpr float texel = 2.0f * radius / Options::Rendering::SHADOW_RESOLUTION;
    // Include upstream casters throughout the loaded volume, even outside the
    // receiver radius. Depth range follows world size rather than camera pitch.
    constexpr float halfDepth = (Options::World::MESH_RADIUS + 2) * Options::World::CHUNK_SIZE * 2.0f;
    view[3] = glm::vec4(-std::round(center.x / texel) * texel,
        -std::round(center.y / texel) * texel, -center.z - halfDepth, 1);
    auto projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1f, halfDepth * 2.0f);
    projection[1][1] *= -1.0f;

    return projection * view;
}
