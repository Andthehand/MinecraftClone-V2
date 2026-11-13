#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>
#include <memory>

#include "rendering/Camera.h"
#include "rendering/Sun.h"
#include "world/World.h"
#include "rendering/ChunkRenderCache.h"

class DebugUI;

class Renderer {
public:
    Renderer(RealRHI::Device& device, RealRHI::Swapchain& swapchain);
    ~Renderer() = default;

    bool Initialize();
    bool DrawFrame(const RealRHI::FrameContext& frameContext, const Camera& camera, const World& world, DebugUI& ui);
    bool ApplyChunkUpdates(const ChunkUpdates& updates) { return m_ChunkCache.Apply(updates); }
    void ToggleWireframe() { m_WireframeEnabled = !m_WireframeEnabled; }
    bool IsWireframeEnabled() const { return m_WireframeEnabled; }

private:
    bool CreateVoxelShader();
    bool CreateSkyShader();
    bool CreateShadowShader();

    struct FrameResources;
    void CollectVisibleChunks(RealRHI::RenderGraph& graph, FrameResources& resources, const World& world, ChunkCoord player,
        const Camera& camera);
    void BufferDataInitPass(RealRHI::RenderGraph& graph, FrameResources& resources);
    void AddShadowMapPasses(RealRHI::RenderGraph& graph, FrameResources& resources);
    void AddVoxelPass(RealRHI::RenderGraph& graph, FrameResources& resources,
        const RealRHI::FrameContext& frame, const Camera& camera, RealRHI::RenderGraphResource backbuffer);
private:
    RealRHI::Device& m_Device;
    RealRHI::Swapchain& m_Swapchain;
    std::unique_ptr<RealRHI::TransientResourcePool> m_Pool;

    RealRHI::Ref<RealRHI::Shader> m_VoxelShader;
    RealRHI::Ref<RealRHI::Shader> m_SkyShader;
    RealRHI::Ref<RealRHI::Shader> m_ShadowDepthShader;

    RealRHI::Ref<RealRHI::Texture> m_VoxelAtlas;
    RealRHI::Ref<RealRHI::Sampler> m_VoxelSampler;
    RealRHI::Ref<RealRHI::Sampler> m_ShadowComparisonSampler;
    bool m_WireframeEnabled = false;
    Sun m_Sun;

    ChunkRenderCache m_ChunkCache;
};
