#include "app/Application.h"
#include "app/CameraController.h"
#include "config/Settings.h"
#include "rendering/Renderer.h"
#include "diagnostics/Profiling.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <format>
#include <iostream>
#include <print>

using namespace RealRHI;

namespace {
void CaptureDebugMessage(const DebugMessage& message) {
    const char* severityStr = "";
    switch (message.severity) {
    case DebugSeverity::Trace:
        severityStr = "Trace";
        break;
    case DebugSeverity::Info:
        severityStr = "Info";
        break;
    case DebugSeverity::Warning:
        severityStr = "Warning";
        break;
    case DebugSeverity::Error:
        severityStr = "Error";
        break;
    }

    const char* typeStr = "";
    switch (message.type) {
    case DebugMessageType::General:
        typeStr = "General";
        break;
    case DebugMessageType::Validation:
        typeStr = "Validation";
        break;
    case DebugMessageType::Performance:
        typeStr = "Performance";
        break;
    case DebugMessageType::ShaderCompilation:
        typeStr = "ShaderCompilation";
        break;
    }

    std::println("[{}] {}: {}", severityStr, typeStr, message.message);
}

}

Application::Application() = default;

Application::~Application() {
    MC_PROFILE_ZONE("Application shutdown");
    if (m_Device) {
        m_Device->WaitIdle();
    }
}

bool Application::Initialize() {
    MC_PROFILE_ZONE("Application initialization");
    const DeviceDesc deviceDesc{
        .applicationName = Options::Application::NAME,
        .shaderDirectory = Options::Application::SHADER_DIRECTORY,
        .enableDebug = Options::Application::ENABLE_GRAPHICS_DEBUG,
        .enableValidationLayers = Options::Application::ENABLE_GRAPHICS_DEBUG,
        .debugCallback = CaptureDebugMessage,
    };
    if (Device::Create(deviceDesc, m_Device) != Result::Success) {
        std::cerr << "Failed to create device.\n";
        return false;
    }

    WindowDesc windowDesc{
        .title = Options::Application::NAME,
        .width = Options::Application::WINDOW_WIDTH,
        .height = Options::Application::WINDOW_HEIGHT,
    };
    if (m_Device->CreateWindow(windowDesc, m_Window) != Result::Success) {
        std::cerr << "Failed to create window.\n";
        return false;
    }

    SwapchainDesc swapchainDesc{
        .window = m_Window.Raw(),
        .enableVSync = Options::Application::ENABLE_VSYNC,
    };
    if (m_Device->CreateSwapchain(swapchainDesc, m_Swapchain) != Result::Success) {
        std::cerr << "Failed to create swapchain.\n";
        return false;
    }

    m_Window->SetTitle(std::format("{} | -- FPS", Options::Application::NAME).c_str());
    m_Renderer = std::make_unique<Renderer>(*m_Device, *m_Swapchain);
    return m_Renderer->Initialize();
}

int Application::Run() {
    if (!Initialize()) {
        return 1;
    }

    auto previousFrameTime = std::chrono::steady_clock::now();
    m_FrameCounter.Reset();
    while (true) {
        {
            MC_PROFILE_ZONE("Frame");
            if (!ProcessEvents()) {
                break;
            }
            const auto currentFrameTime = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
            previousFrameTime = currentFrameTime;

            OnUpdate(deltaTime);
            if (!OnRender(deltaTime)) {
                return 1;
            }
        }
        MC_PROFILE_FRAME();
    }
    return 0;
}

bool Application::ProcessEvents() {
    MC_PROFILE_ZONE("Process events");
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F3 && !event.key.repeat) {
            m_Renderer->ToggleWireframe();
        }
    }

    return running;
}

void Application::OnUpdate(float deltaTime) {
    MC_PROFILE_ZONE("Update camera");
    UpdateCameraFromKeyboard(m_Camera, deltaTime);
}

bool Application::OnRender(float deltaTime) {
    MC_PROFILE_ZONE("Render frame");
    const FrameContext frame = m_Swapchain->BeginFrame();
    if (!frame.backBufferView) {
        if (m_Window->IsMinimized() || m_Window->GetWidth() == 0 || m_Window->GetHeight() == 0) {
            SDL_Delay(Options::Application::MINIMIZED_FRAME_DELAY_MS);
            m_FrameCounter.Reset();

            return true;
        }

        std::cerr << "Failed to acquire a backbuffer.\n";
        return false;
    }

    m_Camera.SetAspectRatio(float(frame.width) / float(frame.height));

    {
        const auto updates = m_ChunkStreamer.Update(m_World, m_Camera.GetPosition());
        if (!m_Renderer->ApplyChunkUpdates(updates)) return false;
    }

    if (!m_Renderer->DrawFrame(frame, m_Camera, m_World)) {
        return false;
    }

    if (const auto fps = m_FrameCounter.CountFrame(deltaTime)) {
        m_Window->SetTitle(std::format("{} | {:.0f} FPS", Options::Application::NAME, *fps).c_str());
    }
    return true;
}

