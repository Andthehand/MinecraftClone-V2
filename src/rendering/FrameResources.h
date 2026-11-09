#pragma once

#include "rendering/Renderer.h"
#include "rendering/ShadowMaps.h"
#include <array>
#include <vector>

struct Renderer::FrameResources {
    struct VisibleChunk {
        ChunkCoord coord;
        ChunkRenderMesh* chunk;
    };
    struct ShadowVisibleChunk {
        ChunkCoord coord;
        ChunkRenderMesh* chunk;
        uint32_t cascadeMask; // Bit i marks visibility in cascade i.
    };
    std::vector<VisibleChunk> visibleChunks;
    std::vector<ShadowVisibleChunk> shadowVisibleChunks;
    std::vector<RealRHI::RenderGraphUse> geometryResourceUses;

    RealRHI::RenderGraphResource sunBuffer;
    glm::vec3 sunDirection;

    ShadowMaps::SceneData shadowMapData;
    RealRHI::RenderGraphResource shadowTexture;
    RealRHI::RenderGraphResource shadowDataBuffer;
    RealRHI::Ref<RealRHI::TextureView> sampledShadowView;
    std::array<RealRHI::Ref<RealRHI::TextureView>, 3> shadowDepthViews;
};

