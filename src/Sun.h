#pragma once

#include <chrono>
#include <glm/vec3.hpp>

class Sun {
public:
    Sun();
	~Sun() = default;

    glm::vec3 GetDirection() const;
    glm::vec3 GetOrbitAxis() const;
    float GetAngularRadiusTangent() const;

private:
    const std::chrono::steady_clock::time_point m_StartTime = std::chrono::steady_clock::now();
    glm::vec3 m_OrbitAxis;
    float m_AngularRadiusTangent;
};
