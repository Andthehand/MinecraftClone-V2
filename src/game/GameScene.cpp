#include "game/GameScene.h"
#include "config/Settings.h"
#include "world/World.h"
#include "world/Block.h"
#include "diagnostics/Profiling.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

GameScene::GameScene() {
    m_Player = m_Registry.create();
    m_Registry.emplace<Game::Transform>(m_Player, Options::Camera::START_POSITION,
                                        glm::radians(Options::Camera::START_YAW_DEGREES),
                                        glm::radians(Options::Camera::START_PITCH_DEGREES));
    m_Registry.emplace<Game::PlayerControlled>(m_Player);
}

const Game::Transform* GameScene::GetPlayerTransform() const {
    return m_Registry.valid(m_Player) ? m_Registry.try_get<Game::Transform>(m_Player) : nullptr;
}

void GameScene::Update(const Game::PlayerInput& input, float deltaTime) {
    MC_PROFILE_ZONE("Update game scene");

    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }

    for (auto [entity, transform] : m_Registry.view<Game::PlayerControlled, Game::Transform>().each()) {
        const float cosPitch = glm::cos(transform.pitch);
        const glm::vec3 forward{cosPitch * glm::cos(transform.yaw), glm::sin(transform.pitch),
                                cosPitch * glm::sin(transform.yaw)};
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0, 1, 0}));

        // Fly along the view direction, strafe locally, and move vertically in world space.
        transform.position += (forward * input.forward + right * input.right + glm::vec3{0, 1, 0} * input.up) *
                              Options::Camera::MOVE_SPEED_UNITS_PER_SECOND * deltaTime;

        const float turn = Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime;
        const float pitchLimit = glm::half_pi<float>() - Options::Camera::PITCH_LIMIT_MARGIN_RADIANS;
        transform.yaw += input.yaw * turn;
        transform.pitch = glm::clamp(transform.pitch + input.pitch * turn, -pitchLimit, pitchLimit);
    }
}

void GameScene::EditBlock(World& world, glm::vec3 direction, bool place, uint32_t block) {
    const auto* player = GetPlayerTransform();

    if (!player) {
        return;
    }

    const auto hit = world.Raycast(player->position, direction, Options::Game::BLOCK_INTERACTION_REACH);

    if (!hit) {
        return;
    }

    if (!place) {
        world.SetBlock(hit->position, Blocks::Air);
        return;
    }

    if (hit->normal == glm::ivec3(0)) {
        return;
    }

    const auto position = hit->position + hit->normal;

    if (position == glm::ivec3(glm::floor(player->position))) {
        return;
    }

    if (world.GetBlock(position) == Blocks::Air) {
        world.SetBlock(position, block);
    }
}
