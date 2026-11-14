#pragma once

#include "game/Components.h"

#include <entt/entity/registry.hpp>

namespace Game {
// Held input for this frame, read once by the application.
struct PlayerInput {
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};
} // namespace Game

class World;

class GameScene {
public:
    GameScene();

    void Update(const Game::PlayerInput& input, float deltaTime);
    void EditBlock(World& world, glm::vec3 direction, bool place, uint32_t block);

    [[nodiscard]] entt::entity GetPlayer() const { return m_Player; }

    [[nodiscard]] const Game::Transform* GetPlayerTransform() const;

    // Create/destroy entities between updates, outside active registry iteration.
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }

    [[nodiscard]] const entt::registry& GetRegistry() const { return m_Registry; }

private:
    entt::registry m_Registry;
    entt::entity m_Player = entt::null;
};
