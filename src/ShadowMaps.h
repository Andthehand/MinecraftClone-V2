#pragma once
#include "Camera.h"
#include "Options.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <cstddef>

namespace ShadowMaps {
struct SceneData {
    glm::mat4 cameraView;
    std::array<glm::mat4, 3> lightMatrices;
    glm::vec4 splits; // View-depth splits and overlap fraction.
    glm::vec4 settings; // Depth bias, inverse map size, fade start/end.
    glm::uvec4 indices; // Sampled depth array and comparison sampler.
    std::array<glm::vec4, 18> receiverPlanes;
};
static_assert(sizeof(SceneData) == 592);
static_assert(offsetof(SceneData, lightMatrices) == 64);
static_assert(offsetof(SceneData, splits) == 256);
static_assert(offsetof(SceneData, settings) == 272);
static_assert(offsetof(SceneData, indices) == 288);
static_assert(offsetof(SceneData, receiverPlanes) == 304);

inline std::array<glm::vec3, 8> FrustumCorners(const Camera& camera, float aspect, float nearPlane, float farPlane) {
    std::array<glm::vec3, 8> corners;
    size_t index = 0;
    const float tangent = std::tan(camera.GetFovYRadians() * 0.5f);
    for (float distance : {nearPlane, farPlane}) for (float y : {-1.0f, 1.0f}) for (float x : {-1.0f, 1.0f}) {
        corners[index++] = camera.GetPosition() + camera.GetForward() * distance
            + camera.GetRight() * (x * tangent * distance * aspect) + camera.GetUp() * (y * tangent * distance);
    }

    return corners;
}
inline glm::mat4 LightMatrix(const Camera& camera, float aspect, glm::vec3 sun, float nearPlane, float farPlane) {
    const auto corners = FrustumCorners(camera, aspect, nearPlane, farPlane);
    glm::vec3 center(0);
	
    // Average the frustum corners to find the center of the receiver frustum.
    for (const auto& corner : corners) {
        center += corner / 8.0f;
    }

	// The sun is far away, so we can use an orthographic projection to capture the receiver frustum.
    const auto up = std::abs(sun.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const auto view = glm::lookAtRH(center + sun * 256.0f, center, up);
    glm::vec3 minimum(1e30f), maximum(-1e30f);
    for (const auto& corner : corners) {
        const auto light = glm::vec3(view * glm::vec4(corner, 1));
        minimum = glm::min(minimum, light); maximum = glm::max(maximum, light);
    }

    // Include off-camera casters toward the sun, beyond the receiver frustum.
    auto projection = glm::orthoRH_ZO(minimum.x - 1, maximum.x + 1, minimum.y - 1, maximum.y + 1,
        -maximum.z - Options::Rendering::SHADOW_CASTER_DEPTH_EXTENSION - 1, -minimum.z + 1);
    projection[1][1] *= -1;
    projection[3][1] *= -1;

    return projection * view;
}

inline std::array<glm::vec4, 6> MatrixPlanes(const glm::mat4& matrix) {
    const auto row = [&matrix](int i) { return glm::vec4(matrix[0][i], matrix[1][i], matrix[2][i], matrix[3][i]); };
    std::array<glm::vec4, 6> planes{
        row(3) + row(0),    row(3) - row(0),   row(3) + row(1),
        row(3) - row(1),    row(2),            row(3) - row(2)};
    
	// Normalize the plane equations to ensure consistent distance calculations.
    for (auto& plane : planes) {
        plane /= glm::length(glm::vec3(plane));
    }

    return planes;
}

inline glm::vec4 ReceiverPlane(const glm::mat4& matrix, glm::vec3 normal) {
    // Inverse-transpose of the orthographic linear transform. Its rows are orthogonal.
    glm::vec3 plane;
    for (int row = 0; row < 3; ++row) {
        const glm::vec3 axis(matrix[0][row], matrix[1][row], matrix[2][row]);
        plane[row] = glm::dot(axis, normal) / glm::dot(axis, axis);
    }

    if (std::abs(plane.z) < 1e-8f) {
        return glm::vec4(0);
    }

    return {-2.0f * plane.x / plane.z, -2.0f * plane.y / plane.z, 0, 0};
}


inline SceneData BuildData(const Camera& camera, glm::vec3 sun, uint32_t width, uint32_t height) {
    SceneData data{
		.cameraView = camera.GetViewMatrix(),
        .splits = { Options::Rendering::SHADOW_FADE_END_DISTANCE * 0.25f,
            Options::Rendering::SHADOW_FADE_END_DISTANCE * (7.0f / 12.0f),
            Options::Rendering::SHADOW_FADE_END_DISTANCE, Options::Rendering::SHADOW_CASCADE_OVERLAP_FRACTION },
		.settings = { 0.000002f, 1.0f / Options::Rendering::SHADOW_MAP_RESOLUTION,
			Options::Rendering::SHADOW_FADE_START_DISTANCE, Options::Rendering::SHADOW_FADE_END_DISTANCE },
		
    };
    
    for (int i = 0; i < 3; ++i) {
        const float nearPlane = i == 0 ? Options::Camera::NEAR_CLIP_DISTANCE : data.splits[i - 1] * (1.0f - data.splits.w);
        data.lightMatrices[i] = LightMatrix(camera, float(width) / height, sun, nearPlane, data.splits[i]);
        for (int face = 0; face < 6; ++face) {
            glm::vec3 normal(0); 
            normal[face / 2] = face % 2 == 0 ? 1.0f : -1.0f;

            data.receiverPlanes[i * 6 + face] = ReceiverPlane(data.lightMatrices[i], normal);
        }
    }

    return data;
}
} // namespace ShadowMaps
