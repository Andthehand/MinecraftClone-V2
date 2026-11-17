#pragma once

#include <Device.h>

#include <memory>

#include "rendering/Renderer.h"
#include "game/GameScene.h"
#include "diagnostics/FrameCounter.h"
#include "diagnostics/DebugUI.h"

class Application {
public:
    Application();
    ~Application();

    int Run();
    void OnUpdate(float deltaTime);
    bool OnRender(float deltaTime);

private:
    bool Initialize();
    void ProcessEvents();

private:
    // Keep the device alive until all graphics resources are released.
    std::unique_ptr<RealRHI::Device> m_Device;
    RealRHI::Ref<RealRHI::Window> m_Window;
    RealRHI::Ref<RealRHI::Swapchain> m_Swapchain;

    GameScene m_GameScene;
    std::unique_ptr<Renderer> m_Renderer;
    DebugUI m_DebugUI;

    bool m_Running = true;

    FrameCounter m_FrameCounter;
    double m_LastFps = 0.0;
};
