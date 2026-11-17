#pragma once

#include "game/Components.h"
#include "rendering/Camera.h"
#include "world/Block.h"
#include "world/ChunkStreamer.h"

#include <entt/entity/registry.hpp>

namespace RealRHI {
struct FrameContext;
}

class Renderer;
class DebugUI;

namespace Game {
// Held input for this frame, read once by the application.
struct PlayerInput {
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

enum class BlockAction {
    Break,
    Place,
};
} // namespace Game

class GameScene {
public:
    GameScene();

    void OnUpdate(const Game::PlayerInput& input, float deltaTime);
    [[nodiscard]] bool OnRender(const RealRHI::FrameContext& frame, Renderer& renderer, DebugUI& ui);

    void SelectBlock(uint32_t block);
    void EditBlock(const CameraRay& ray, Game::BlockAction action);

    [[nodiscard]] const Camera& GetCamera() const { return m_Camera; }
    [[nodiscard]] const World& GetWorld() const { return m_World; }
    [[nodiscard]] uint32_t GetSelectedBlock() const { return m_SelectedBlock; }

    [[nodiscard]] entt::entity GetPlayer() const { return m_Player; }

    [[nodiscard]] const Game::Transform* GetPlayerTransform() const;

    // Create/destroy entities between updates, outside active registry iteration.
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }

    [[nodiscard]] const entt::registry& GetRegistry() const { return m_Registry; }

private:
    void UpdatePlayer(const Game::PlayerInput& input, float deltaTime);

private:
    World m_World;
    Camera m_Camera;
    ChunkStreamer m_ChunkStreamer;
    entt::registry m_Registry;
    entt::entity m_Player = entt::null;
    uint32_t m_SelectedBlock = Blocks::Grass;
};
