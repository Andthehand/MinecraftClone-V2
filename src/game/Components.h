#pragma once

#include <glm/vec3.hpp>

namespace Game {
struct Transform {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct PlayerControlled {};
} // namespace Game
