#include "game/GameScene.h"
#include "config/Settings.h"
#include "world/World.h"
#include "world/Block.h"
#include "world/BlockRegistry.h"
#include "diagnostics/Profiling.h"
#include "rendering/Renderer.h"

#include <FrameContext.h>

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
    return m_Registry.try_get<Game::Transform>(m_Player);
}

void GameScene::OnUpdate(const Game::PlayerInput& input, float deltaTime) {
    MC_PROFILE_ZONE("Update game scene");

    UpdatePlayer(input, deltaTime);
    if (const auto* transform = GetPlayerTransform()) {
        m_Camera.SetPosition(transform->position);
        m_Camera.SetYawPitch(transform->yaw, transform->pitch);
    }
}

bool GameScene::OnRender(const RealRHI::FrameContext& frame, Renderer& renderer, DebugUI& ui) {
    MC_PROFILE_ZONE("Render game scene");

    m_Camera.SetAspectRatio(float(frame.width) / float(frame.height));

    {
        const auto* player = GetPlayerTransform();
        const auto updates = m_ChunkStreamer.Update(m_World, player->position);

        if (!renderer.ApplyChunkUpdates(updates)) {
            return false;
        }
    }

    return renderer.DrawFrame(frame, m_Camera, m_World, ui);
}

void GameScene::UpdatePlayer(const Game::PlayerInput& input, float deltaTime) {
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

void GameScene::SelectBlock(uint32_t block) {
    if (block > Blocks::Air && block < Blocks::Registry().All().size()) {
        m_SelectedBlock = block;
    }
}

void GameScene::EditBlock(const CameraRay& ray, Game::BlockAction action) {
    const auto* player = GetPlayerTransform();

    if (!player) {
        return;
    }

    const auto hit = m_World.Raycast(ray.origin, ray.direction, Options::Game::BLOCK_INTERACTION_REACH);

    if (!hit) {
        return;
    }

    if (action == Game::BlockAction::Break) {
        m_World.SetBlock(hit->position, Blocks::Air);
        return;
    }

    if (hit->normal == glm::ivec3(0)) {
        return;
    }

    const auto position = hit->position + hit->normal;

    if (position == glm::ivec3(glm::floor(player->position))) {
        return;
    }

    if (m_World.GetBlock(position) == Blocks::Air) {
        m_World.SetBlock(position, m_SelectedBlock);
    }
}
