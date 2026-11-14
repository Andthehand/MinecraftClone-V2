#include "diagnostics/DebugUI.h"

#if MINECRAFT_ENABLE_IMGUI

#include "world/Block.h"

#include <ImGui/ImGuiRenderer.h>
#include <SDL3/SDL.h>
#include <algorithm>

using namespace RealRHI;

struct DebugUI::State {
    ImGuiRenderer renderer;
    ImGuiContext* context = nullptr;
    bool show = true;
    bool showDemo = false;
    Result renderResult = Result::Success;

    ~State() {
        if (context) {
            ImGui::SetCurrentContext(context);
            renderer.Shutdown();
            ImGui::DestroyContext(context);
        }
    }
};

DebugUI::DebugUI() : m_State(std::make_unique<State>()) {
}

DebugUI::~DebugUI() = default;

bool DebugUI::Initialize(Device& device, Window& window) {
    IMGUI_CHECKVERSION();
    m_State->context = ImGui::CreateContext();
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    const float scale = std::max(1.0f, window.GetDisplayScale());
    style.ScaleAllSizes(scale);
    style.WindowRounding = 0;
    style.Colors[ImGuiCol_WindowBg].w = 1;
    ImFontConfig font;
    font.SizePixels = 13.0f * scale;
    ImGui::GetIO().Fonts->AddFontDefault(&font);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    return m_State->renderer.Initialize(device) == Result::Success &&
           m_State->renderer.InitializePlatform(window) == Result::Success;
}

bool DebugUI::IsMainWindow(uint32_t windowID) const {
    return windowID == static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle));
}

void DebugUI::ProcessEvent(const SDL_Event& event) {
    m_State->renderer.ProcessEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN && IsMainWindow(event.key.windowID) && event.key.scancode == SDL_SCANCODE_F1 &&
        !event.key.repeat) {
        m_State->show = !m_State->show;

        if (!m_State->show) {
            m_State->showDemo = false;
        }
    }
}

bool DebugUI::WantsKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUI::WantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::HasVisibleSecondaryWindows() const {
    const auto& viewports = ImGui::GetPlatformIO().Viewports;

    for (int i = 1; i < viewports.Size; ++i) {
        auto* window =
            SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewports[i]->PlatformHandle)));

        if (window && (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) == 0) {
            return true;
        }
    }

    return false;
}

void DebugUI::BeginFrame(const Camera& camera, Renderer& renderer, uint32_t& selectedBlock, double fps) {
    m_State->renderer.NewFrame();
    ImGui::NewFrame();

    if (!m_State->show) {
        return;
    }

    // Preserve the terrain behind the dockspace's empty central area.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12, viewport->WorkPos.y + 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetFontSize() * 32, viewport->WorkSize.x - 24), 0),
                             ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Minecraft debug", &m_State->show)) {
        ImGui::Text("%.0f FPS | %.2f ms", fps, 1000.0f / ImGui::GetIO().Framerate);
        const auto position = camera.GetPosition();
        ImGui::Text("Position: %.1f, %.1f, %.1f", position.x, position.y, position.z);
        bool wireframe = renderer.IsWireframeEnabled();

        if (ImGui::Checkbox("Wireframe (F3)", &wireframe)) {
            renderer.ToggleWireframe();
        }

        int block = selectedBlock == Blocks::Grass ? 0 : selectedBlock == Blocks::Dirt ? 1 : 2;

        if (ImGui::Combo("Block", &block, "Grass\0Dirt\0Stone\0")) {
            selectedBlock = block == 0 ? Blocks::Grass : block == 1 ? Blocks::Dirt : Blocks::Stone;
        }

        ImGui::Checkbox("Show ImGui demo", &m_State->showDemo);
        ImGui::TextUnformatted("F1: toggle UI | Drag a tab to dock or detach it.");
    }

    ImGui::End();

    if (m_State->show && m_State->showDemo) {
        ImGui::ShowDemoWindow(&m_State->showDemo);
    }
}

bool DebugUI::EndFrame() {
    ImGui::Render();

    return m_State->renderer.RenderPlatformWindows() == Result::Success;
}

bool DebugUI::AddRenderPass(RenderGraph& graph, RenderGraphResource backbuffer, const FrameContext& frame,
                            TextureFormat format) {
    m_State->renderResult = Result::Success;
    const auto* drawData = ImGui::GetDrawData();

    if (!drawData || drawData->TotalVtxCount == 0) {
        return true;
    }

    constexpr ResourceState color{PipelineStage::ColorAttachmentOutput,
                                  ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite};
    constexpr ResourceState sampled{PipelineStage::FragmentShader, ResourceAccess::ShaderRead};
    std::vector<RenderGraphUse> uses{
        {backbuffer, color, TextureLayout::ColorAttachment, RenderGraphAccess::ReadWrite},
    };
    std::vector<Texture*> textures;

    for (const auto* list : drawData->CmdLists) {
        for (const auto& draw : list->CmdBuffer) {
            if (draw.UserCallback || draw.ElemCount == 0) {
                continue;
            }

            auto* texture = reinterpret_cast<Texture*>(static_cast<uintptr_t>(draw.GetTexID()));

            if (!texture) {
                return false;
            }

            if (std::find(textures.begin(), textures.end(), texture) != textures.end()) {
                continue;
            }

            textures.push_back(texture);
            uses.push_back({graph.ImportTexture(texture, TextureLayout::ShaderRead, sampled), sampled,
                            TextureLayout::ShaderRead, RenderGraphAccess::Read});
        }
    }

    const auto colorSpace = (format == TextureFormat::RGBA8_SRGB || format == TextureFormat::BGRA8_SRGB)
                                ? ImGuiColorSpace::Linear
                                : ImGuiColorSpace::SRGB;
    graph.AddPass({.name = "ImGui", .resources = std::move(uses)},
                  [this, drawData, frame, colorSpace](RenderGraphContext& context) {
                      auto& commands = context.GetCommands();
                      commands.BeginRendering({
                          .colorAttachments = {{.target = frame.backBufferView, .loadOp = LoadOp::Load}},
                          .renderArea = {.width = frame.width, .height = frame.height},
                      });
                      m_State->renderResult = m_State->renderer.RenderDrawData(commands, *drawData, 1, colorSpace);
                      commands.EndRendering();
                  });

    return true;
}

bool DebugUI::RenderSucceeded() const {
    return m_State->renderResult == Result::Success;
}

#else

struct DebugUI::State {};

DebugUI::DebugUI() = default;
DebugUI::~DebugUI() = default;

bool DebugUI::Initialize(RealRHI::Device&, RealRHI::Window&) {
    return true;
}

void DebugUI::ProcessEvent(const SDL_Event&) {
}

bool DebugUI::IsMainWindow(uint32_t) const {
    return true;
}

bool DebugUI::WantsKeyboard() const {
    return false;
}

bool DebugUI::WantsMouse() const {
    return false;
}

bool DebugUI::HasVisibleSecondaryWindows() const {
    return false;
}

void DebugUI::BeginFrame(const Camera&, Renderer&, uint32_t&, double) {
}

bool DebugUI::EndFrame() {
    return true;
}

bool DebugUI::AddRenderPass(RealRHI::RenderGraph&, RealRHI::RenderGraphResource, const RealRHI::FrameContext&,
                            RealRHI::TextureFormat) {
    return true;
}

bool DebugUI::RenderSucceeded() const {
    return true;
}

#endif
