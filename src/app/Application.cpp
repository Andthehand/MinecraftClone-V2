#include "app/Application.h"

#include "config/Settings.h"
#include "rendering/Renderer.h"
#include "diagnostics/Profiling.h"
#include "world/BlockRegistry.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <print>
#include <string>

using namespace RealRHI;

namespace {
Game::PlayerInput ReadPlayerInputFromKeyboard() {
    const bool* keyboard = SDL_GetKeyboardState(nullptr);

    return {
        .forward = float(keyboard[SDL_SCANCODE_W]) - float(keyboard[SDL_SCANCODE_S]),
        .right = float(keyboard[SDL_SCANCODE_D]) - float(keyboard[SDL_SCANCODE_A]),
        .up = float(keyboard[SDL_SCANCODE_SPACE]) - float(keyboard[SDL_SCANCODE_LCTRL]),
        .yaw = float(keyboard[SDL_SCANCODE_RIGHT]) - float(keyboard[SDL_SCANCODE_LEFT]),
        .pitch = float(keyboard[SDL_SCANCODE_UP]) - float(keyboard[SDL_SCANCODE_DOWN]),
    };
}

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

} // namespace

Application::Application() {
    m_Running = Initialize();
}

bool Application::Initialize() {
    MC_PROFILE_ZONE("Application initialization");

    // Resolve content beside the executable, regardless of the launch working directory.
    const char* base = SDL_GetBasePath();
    if (!base) {
        std::cerr << "Failed to locate application directory: " << SDL_GetError() << '\n';

        return false;
    }

    const auto blocks = Blocks::Initialize(std::filesystem::path(base) / "assets");
    if (!blocks) {
        std::cerr << "Failed to load block definitions: " << blocks.error() << '\n';

        return false;
    }

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
        .enableHighPixelDensity = true,
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

    m_Renderer = std::make_unique<Renderer>(*m_Device, *m_Swapchain);
    if (!m_Renderer->Initialize()) {
        return false;
    }

    if (!m_DebugUI.Initialize(*m_Device, *m_Window)) {
        std::cerr << "Failed to initialize debug UI.\n";

        return false;
    }

    return true;
}

Application::~Application() {
    MC_PROFILE_ZONE("Application shutdown");

    if (m_Device) {
        m_Device->WaitIdle();
    }
}

int Application::Run() {
    auto previousFrameTime = std::chrono::steady_clock::now();

    while (m_Running) {
        MC_PROFILE_ZONE("Frame");

        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
        previousFrameTime = currentFrameTime;

        ProcessEvents();

        auto selectedBlock = m_GameScene.GetSelectedBlock();
        m_DebugUI.BeginFrame(m_GameScene.GetCamera(), *m_Renderer, selectedBlock, m_LastFps);
        m_GameScene.SelectBlock(selectedBlock);
        OnUpdate(deltaTime);

        if (!m_DebugUI.EndFrame()) {
            std::cerr << "Failed to render debug UI platform windows.\n";

            return 1;
        }

        if (!OnRender(deltaTime)) {
            return 1;
        }

        MC_PROFILE_FRAME();
    }

    return 0;
}

void Application::ProcessEvents() {
    MC_PROFILE_ZONE("Process events");
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        m_DebugUI.ProcessEvent(event);

        if (event.type == SDL_EVENT_KEY_DOWN &&
            (!m_DebugUI.IsMainWindow(event.key.windowID) || m_DebugUI.WantsKeyboard())) {
            continue;
        }

        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && m_DebugUI.IsMainWindow(event.window.windowID))) {
            m_Running = false;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F3 && !event.key.repeat) {
            m_Renderer->ToggleWireframe();
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= SDL_SCANCODE_9) {
                const auto id = static_cast<uint32_t>(event.key.scancode - SDL_SCANCODE_1 + 1);
                m_GameScene.SelectBlock(id);
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && m_DebugUI.IsMainWindow(event.button.windowID) &&
            !m_DebugUI.WantsMouse() &&
            (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
            uint32_t width = 0, height = 0;
            // Mouse coordinates and window size both use logical pixels, including on high-DPI displays.
            if (m_Window->GetSize(width, height) == Result::Success) {
                const auto ray = m_GameScene.GetCamera().GetPickRay(
                event.button.x, event.button.y, static_cast<int>(width), static_cast<int>(height));

                const auto action = event.button.button == SDL_BUTTON_RIGHT ? Game::BlockAction::Place
                                                                            : Game::BlockAction::Break;
                m_GameScene.EditBlock(ray, action);
            }
        }
    }
}

void Application::OnUpdate(float deltaTime) {
    Game::PlayerInput input;

    if (m_Window->HasInputFocus() && !m_DebugUI.WantsKeyboard()) {
        input = ReadPlayerInputFromKeyboard();
    }

    m_GameScene.OnUpdate(input, deltaTime);
}

bool Application::OnRender(float deltaTime) {
    MC_PROFILE_ZONE("Render frame");

    if (m_Window->IsMinimized() || m_Window->GetWidth() == 0 || m_Window->GetHeight() == 0) {
        SDL_Delay(Options::Application::MINIMIZED_FRAME_DELAY_MS);
        m_FrameCounter.Reset();

        return true;
    }

    const FrameContext frame = m_Swapchain->BeginFrame();

    if (!m_GameScene.OnRender(frame, *m_Renderer, m_DebugUI)) {
        return false;
    }

    if (const auto fps = m_FrameCounter.CountFrame(deltaTime)) {
        m_LastFps = *fps;
    }

    return true;
}
