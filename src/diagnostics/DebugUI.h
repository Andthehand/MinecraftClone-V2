#pragma once

#include <RenderGraph/RenderGraph.h>
#include <SDL3/SDL_events.h>
#include <Window.h>
#include <cstdint>
#include <memory>

#include "rendering/Camera.h"
#include "rendering/Renderer.h"

// Keeps optional debug UI details out of the application and scene renderer.
class DebugUI {
public:
    DebugUI();
    ~DebugUI();
    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    bool Initialize(RealRHI::Device& device, RealRHI::Window& window);
    void ProcessEvent(const SDL_Event& event);
    bool IsMainWindow(uint32_t windowID) const;
    bool WantsKeyboard() const;
    bool WantsMouse() const;
    bool HasVisibleSecondaryWindows() const;

    void BeginFrame(const Camera& camera, Renderer& renderer, uint32_t& selectedBlock, double fps);
    // Finish the UI and draw detached windows before main swapchain acquisition.
    bool EndFrame();
    bool AddRenderPass(RealRHI::RenderGraph& graph, RealRHI::RenderGraphResource backbuffer,
                       const RealRHI::FrameContext& frame, RealRHI::TextureFormat format);
    bool RenderSucceeded() const;

private:
    struct State;
    std::unique_ptr<State> m_State;
};
