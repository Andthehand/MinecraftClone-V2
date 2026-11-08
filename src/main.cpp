#include <Device.h>
using namespace RealRHI;

#include <SDL3/SDL.h>

#include <chrono>
#include <format>
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
			.enableDebug = Options::Application::ENABLE_GRAPHICS_DEBUG,
			.enableValidationLayers = Options::Application::ENABLE_GRAPHICS_DEBUG,
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
	if (device->CreateSwapchain(SwapchainDesc{ .window = window.Raw(), .enableVSync = Options::Application::ENABLE_VSYNC }, swapchain) != Result::Success) {
		std::cerr << "Failed to create swapchain.\n";
		return 1;
	}

	// The game owns a single SDL window; keep the counter in its title bar.
	SDL_Window** sdlWindows = SDL_GetWindows(nullptr);
	SDL_Window* sdlWindow = sdlWindows ? sdlWindows[0] : nullptr;
	SDL_free(sdlWindows);
	if (!sdlWindow) {
		std::cerr << "Failed to find the game window for the FPS counter.\n";
		return 1;
	}
	SDL_SetWindowTitle(sdlWindow, std::format("{} | -- FPS", Options::Application::NAME).c_str());

	Renderer renderer(*device, *swapchain);
	Camera camera;
	auto previousFrameTime = std::chrono::steady_clock::now();
	auto fpsSampleStart = previousFrameTime;
	uint64_t fpsFrameCount = 0;
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

		constexpr float MOVE_SPEED_UNITS_PER_SECOND = Options::Camera::MOVE_SPEED_UNITS_PER_SECOND;
		camera.MoveLocal(moveForward * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveRight * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveUp * MOVE_SPEED_UNITS_PER_SECOND * deltaTime);
		camera.AddYawPitch(yawInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime, pitchInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime);

		const FrameContext frame = swapchain->BeginFrame();
		if (!frame.backBufferView) {
			if (window->IsMinimized() || window->GetWidth() == 0 || window->GetHeight() == 0) {
				SDL_Delay(Options::Application::MINIMIZED_FRAME_DELAY_MS);
				fpsSampleStart = std::chrono::steady_clock::now();
				fpsFrameCount = 0;
				continue;
			}
			std::cerr << "Failed to acquire a backbuffer.\n";
			return 1;
		}
		camera.SetAspectRatio(float(frame.width) / float(frame.height));
		if (!renderer.DrawFrame(frame, camera)) {
			return 1;
		}

		++fpsFrameCount;
		const auto frameEnd = std::chrono::steady_clock::now();
		const double fpsElapsed = std::chrono::duration<double>(frameEnd - fpsSampleStart).count();
		if (fpsElapsed >= 0.5) {
			const double fps = static_cast<double>(fpsFrameCount) / fpsElapsed;
			SDL_SetWindowTitle(sdlWindow, std::format("{} | {:.0f} FPS", Options::Application::NAME, fps).c_str());
			fpsSampleStart = frameEnd;
			fpsFrameCount = 0;
		}
	}

	device->WaitIdle();
	return 0;
}
