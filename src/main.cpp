#include <Device.h>
using namespace RealRHI;

#include <SDL3/SDL.h>

#include <iostream>
#include <print> // required for std::println

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
			.applicationName = "Minecraft Clone",
			.shaderDirectory = "shaders",
			.enableDebug = true,
			.enableValidationLayers = true,
			.debugCallback = CaptureDebugMessage,
		}, device) != Result::Success) {
		std::cerr << "Failed to create device.\n";
		return 1;
	}

	Ref<Window> window;
	if (device->CreateWindow(WindowDesc{
			.title = "Minecraft Clone",
			.width = 800,
			.height = 600,
		}, window) != Result::Success) {
		std::cerr << "Failed to create window.\n";
		return 1;
	}

	Ref<Swapchain> swapchain;
	if (device->CreateSwapchain(SwapchainDesc{ .window = window.Raw(), .enableStorage = true }, swapchain) != Result::Success) {
		std::cerr << "Failed to create swapchain.\n";
		return 1;
	}

	Renderer renderer(*device, *swapchain);

	bool running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			}
		}
		if (!running) {
			break;
		}

		const FrameContext frame = swapchain->BeginFrame();
		if (!frame.backBufferView) {
			if (window->IsMinimized() || window->GetWidth() == 0 || window->GetHeight() == 0) {
				SDL_Delay(16);
				continue;
			}
			std::cerr << "Failed to acquire a backbuffer.\n";
			return 1;
		}
		if (!renderer.DrawFrame(frame)) {
			return 1;
		}
	}

	device->WaitIdle();
	return 0;
}
