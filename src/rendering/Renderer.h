#pragma once
#include <Device.h>
#include <RenderGraph/RenderGraph.h>
#include <RenderGraph/TransientResourcePool.h>
#include <memory>

#include "rendering/Camera.h"
#include "rendering/Sun.h"
#include "world/World.h"
#include "rendering/ChunkRenderCache.h"

class Renderer {
public:
    Renderer(RealRHI::Device& device, RealRHI::Swapchain& swapchain);
    ~Renderer() = default;

    bool Initialize();
    bool DrawFrame(const RealRHI::FrameContext& frameContext, const Camera& camera, const World& world);
    bool ApplyChunkUpdates(const ChunkUpdates& updates) { return m_ChunkCache.Apply(updates); }
    void ToggleWireframe() { m_WireframeEnabled = !m_WireframeEnabled; }

private:
    bool CreateVoxelPipeline();
    bool CreateSkyPipeline();
    bool CreateShadowPipeline();

    struct FrameResources;
    void CollectVisibleChunks(RealRHI::RenderGraph& graph, FrameResources& resources, const World& world, ChunkCoord player,
        const Camera& camera);
    void BufferDataInitPass(RealRHI::RenderGraph& graph, FrameResources& resources);
    void AddShadowMapPasses(RealRHI::RenderGraph& graph, FrameResources& resources);
    void AddVoxelPass(RealRHI::RenderGraph& graph, FrameResources& resources,
        const RealRHI::FrameContext& frame, const Camera& camera);
private:
    RealRHI::Device& m_Device;
    RealRHI::Swapchain& m_Swapchain;
    std::unique_ptr<RealRHI::TransientResourcePool> m_Pool;

    RealRHI::Ref<RealRHI::Pipeline> m_SolidPipeline;
    RealRHI::Ref<RealRHI::Pipeline> m_WireframePipeline;
    RealRHI::Ref<RealRHI::Pipeline> m_SkyPipeline;
    RealRHI::Ref<RealRHI::Pipeline> m_ShadowDepthPipeline;

    RealRHI::Ref<RealRHI::Texture> m_VoxelAtlas;
    RealRHI::Ref<RealRHI::Sampler> m_VoxelSampler;
    RealRHI::Ref<RealRHI::Sampler> m_ShadowComparisonSampler;
    bool m_WireframeEnabled = false;
    Sun m_Sun;

    ChunkRenderCache m_ChunkCache;
};
