#include "Application.h"
#include "Options.h"
#include "Renderer.h"

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
	if (m_Device) {
		m_Device->WaitIdle();
	}
}

bool Application::Initialize() {
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

	return true;
}

int Application::Run() {
	if (!Initialize()) {
		return 1;
	}

	auto previousFrameTime = std::chrono::steady_clock::now();
	m_FrameCounter.Reset();
	while (ProcessEvents()) {
		const auto currentFrameTime = std::chrono::steady_clock::now();
		const float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
		previousFrameTime = currentFrameTime;

		OnUpdate(deltaTime);
		if (!OnRender(deltaTime)) {
			return 1;
		}
	}
	return 0;
}

bool Application::ProcessEvents() {
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
	const bool* keyboard = SDL_GetKeyboardState(nullptr);
	const float moveForward = (keyboard[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_S] ? 1.0f : 0.0f);
	const float moveRight = (keyboard[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_A] ? 1.0f : 0.0f);
	const float moveUp = (keyboard[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
	const float yawInput = (keyboard[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
	const float pitchInput = (keyboard[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);

	constexpr float MOVE_SPEED_UNITS_PER_SECOND = Options::Camera::MOVE_SPEED_UNITS_PER_SECOND;
	m_Camera.MoveLocal(moveForward * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveRight * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveUp * MOVE_SPEED_UNITS_PER_SECOND * deltaTime);
	m_Camera.AddYawPitch(yawInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime, pitchInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime);
}

bool Application::OnRender(float deltaTime) {
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

	if (!m_Renderer->DrawFrame(frame, m_Camera)) {
		return false;
	}

	if (const auto fps = m_FrameCounter.CountFrame(deltaTime)) {
		m_Window->SetTitle(std::format("{} | {:.0f} FPS", Options::Application::NAME, *fps).c_str());
	}
	return true;
}

