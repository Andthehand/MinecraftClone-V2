#pragma once

#include <Device.h>

#include <memory>

#include "rendering/Camera.h"
#include "world/World.h"
#include "world/ChunkStreamer.h"
#include "diagnostics/FrameCounter.h"

class Renderer;

class Application {
public:
    Application();
    ~Application();

    int Run();
    void OnUpdate(float deltaTime);
    bool OnRender(float deltaTime);

private:
    bool Initialize();
    bool ProcessEvents();

    // Keep the device alive until all graphics resources are released.
    std::unique_ptr<RealRHI::Device> m_Device;
    RealRHI::Ref<RealRHI::Window> m_Window;
    RealRHI::Ref<RealRHI::Swapchain> m_Swapchain;

    Camera m_Camera;
    World m_World;
    ChunkStreamer m_ChunkStreamer;
    std::unique_ptr<Renderer> m_Renderer;

    FrameCounter m_FrameCounter;
};
