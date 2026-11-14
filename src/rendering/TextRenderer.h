#pragma once

#include <Device.h>
#include <RenderGraph/RenderGraph.h>

#include "rendering/TextBatch.h"

class TextRenderer {
public:
    bool Initialize(RealRHI::Device& device);

    bool AddRenderPass(RealRHI::Device& device, RealRHI::RenderGraph& graph, RealRHI::RenderGraphResource backbuffer,
                       const RealRHI::FrameContext& frame, RealRHI::TextureFormat format, const TextBatch& batch);

private:
    RealRHI::Ref<RealRHI::Texture> m_Atlas;
    RealRHI::Ref<RealRHI::Sampler> m_Sampler;
    RealRHI::Ref<RealRHI::Shader> m_Shader;
};
