#include <Device.h>
using namespace RealRHI;

#include <SDL3/SDL.h>

#include <chrono>
#include <charconv>
#include <string_view>
#include <iostream>
#include <print> // required for std::println

#include "Camera.h"
#include "Options.h"
#include "Renderer.h"

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

int main() {
	std::unique_ptr<Device> device;
	if (Device::Create(DeviceDesc{
			.applicationName = Options::Application::NAME,
			.shaderDirectory = Options::Application::SHADER_DIRECTORY,
			.enableDebug = Options::Application::ENABLE_DEBUG,
			.enableValidationLayers = Options::Application::ENABLE_DEBUG,
			.debugCallback = CaptureDebugMessage,
		}, device) != Result::Success) {
		std::cerr << "Failed to create device.\n";
		return 1;
	}

	Ref<Window> window;
	if (device->CreateWindow(WindowDesc{
			.title = Options::Application::NAME,
			.width = Options::Application::WINDOW_WIDTH,
			.height = Options::Application::WINDOW_HEIGHT,
		}, window) != Result::Success) {
		std::cerr << "Failed to create window.\n";
		return 1;
	}

	Ref<Swapchain> swapchain;
	if (device->CreateSwapchain(SwapchainDesc{ .window = window.Raw() }, swapchain) != Result::Success) {
		std::cerr << "Failed to create swapchain.\n";
		return 1;
	}

	Renderer renderer(*device, *swapchain);
	Camera camera;

	auto previousFrameTime = std::chrono::steady_clock::now();
	bool running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			}
			if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F3 && !event.key.repeat) {
				renderer.ToggleWireframe();
			}
		}
		if (!running) {
			break;
		}

		auto currentFrameTime = std::chrono::steady_clock::now();
		const float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
		previousFrameTime = currentFrameTime;

		const bool* keyboard = SDL_GetKeyboardState(nullptr);
		const float moveForward = (keyboard[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_S] ? 1.0f : 0.0f);
		const float moveRight = (keyboard[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_A] ? 1.0f : 0.0f);
		const float moveUp = (keyboard[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
		const float yawInput = (keyboard[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
		const float pitchInput = (keyboard[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);

		constexpr float MOVE_SPEED = Options::Camera::MOVE_SPEED;
		camera.MoveLocal(moveForward * MOVE_SPEED * deltaTime, moveRight * MOVE_SPEED * deltaTime, moveUp * MOVE_SPEED * deltaTime);
		camera.AddYawPitch(yawInput * Options::Camera::TURN_SPEED * deltaTime, pitchInput * Options::Camera::TURN_SPEED * deltaTime);

		const FrameContext frame = swapchain->BeginFrame();
		if (!frame.backBufferView) {
			if (window->IsMinimized() || window->GetWidth() == 0 || window->GetHeight() == 0) {
				SDL_Delay(Options::Application::MINIMIZED_DELAY_MS);
				continue;
			}
			std::cerr << "Failed to acquire a backbuffer.\n";
			return 1;
		}
		if (!renderer.DrawFrame(frame, camera)) {
			return 1;
		}
	}

	device->WaitIdle();
	return 0;
}
