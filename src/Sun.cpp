#include "Sun.h"
#include "Options.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_transform.hpp>

Sun::Sun() {
    const glm::vec3 axis = glm::cross(glm::normalize(Options::Rendering::SUN_DIRECTION), glm::vec3(0, 1, 0));
    m_OrbitAxis = glm::dot(axis, axis) > 0.0001f ? glm::normalize(axis) : glm::vec3(1, 0, 0);
    m_AngularRadiusTangent = glm::tan(glm::radians(Options::Rendering::SUN_ANGULAR_SIZE_DEGREES * 0.5f));
}

glm::vec3 Sun::GetDirection() const {
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
    const float angle = glm::radians(float(360.0 * std::fmod(elapsed, Options::Rendering::SUN_ORBIT_PERIOD_SECONDS)
        / Options::Rendering::SUN_ORBIT_PERIOD_SECONDS));
    const glm::vec3 initial = glm::normalize(Options::Rendering::SUN_DIRECTION);
    
    return glm::normalize(glm::vec3(glm::rotate(glm::mat4(1), angle, m_OrbitAxis) * glm::vec4(initial, 0)));
}

glm::vec3 Sun::GetOrbitAxis() const {
    return m_OrbitAxis;
}

float Sun::GetAngularRadiusTangent() const {
    return m_AngularRadiusTangent;
}
