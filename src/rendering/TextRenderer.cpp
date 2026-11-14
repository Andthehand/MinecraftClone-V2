#include "rendering/TextRenderer.h"
#include "rendering/BitmapFont.h"

#include <limits>

using namespace RealRHI;

bool TextRenderer::Initialize(Device& device) {
    const auto pixels = BitmapFont::BuildAtlas();

    return device.CreateTexture({.width = BitmapFont::WIDTH,
                                 .height = BitmapFont::HEIGHT,
                                 .format = TextureFormat::RGBA8_UNorm,
                                 .usage = TextureUsage::ShaderResource | TextureUsage::TransferDst,
                                 .debugName = "BitmapFontAtlas"},
                                m_Atlas) == Result::Success &&
           m_Atlas->UploadData(pixels.data(), static_cast<uint32_t>(pixels.size())) == Result::Success &&
           device.CreateSampler({.minFilter = Filter::Nearest,
                                 .magFilter = Filter::Nearest,
                                 .mipFilter = Filter::Nearest,
                                 .addressU = SamplerAddressMode::ClampToEdge,
                                 .addressV = SamplerAddressMode::ClampToEdge,
                                 .debugName = "BitmapFontSampler"},
                                m_Sampler) == Result::Success &&
           device.CreateShader({.moduleName = "text.slang", .debugName = "BitmapText"}, m_Shader) == Result::Success;
}

bool TextRenderer::AddRenderPass(Device& device, RenderGraph& graph, RenderGraphResource backbuffer,
                                 const FrameContext& frame, TextureFormat format, const TextBatch& batch) {

    const auto& vertices = batch.Vertices();

    if (vertices.empty() || frame.width == 0 || frame.height == 0) {
        return true;
    }

    if (vertices.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    // Immutable per recording. The graph and command list retain it across GPU frames.
    Ref<Buffer> buffer;

    if (device.CreateBuffer({.size = vertices.size() * sizeof(TextVertex),
                             .usage = BufferUsage::Storage,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = vertices.data(),
                             .debugName = "TextVertices"},
                            buffer) != Result::Success) {
        return false;
    }

    constexpr ResourceState sampled{PipelineStage::FragmentShader, ResourceAccess::ShaderRead};
    constexpr ResourceState vertexRead{PipelineStage::VertexShader, ResourceAccess::ShaderRead};

    const auto atlas = graph.ImportTexture(m_Atlas.Raw(), TextureLayout::ShaderRead, sampled);
    const auto geometry = graph.ImportBuffer(buffer.Raw(), vertexRead);

    const uint32_t count = static_cast<uint32_t>(vertices.size());
    const uint32_t linear = format == TextureFormat::RGBA8_SRGB || format == TextureFormat::BGRA8_SRGB;

    graph.AddPass({.name = "BitmapText",
                   .resources = {{backbuffer,
                                  {PipelineStage::ColorAttachmentOutput,
                                   ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite},
                                  TextureLayout::ColorAttachment,
                                  RenderGraphAccess::ReadWrite},
                                 {atlas, sampled, TextureLayout::ShaderRead, RenderGraphAccess::Read},
                                 {geometry, vertexRead, TextureLayout::Undefined, RenderGraphAccess::Read}}},
                  [this, frame, geometry, count, linear](RenderGraphContext& context) {
                      auto& commands = context.GetCommands();

                      commands.BeginRendering(
                          {.colorAttachments = {{.target = frame.backBufferView, .loadOp = LoadOp::Load}},
                           .renderArea = {.width = frame.width, .height = frame.height}});

                      commands.SetViewport({.width = float(frame.width), .height = float(frame.height)});
                      commands.SetScissor({.width = frame.width, .height = frame.height});

                      commands.BindShaderProgram(m_Shader.Raw());
                      commands.SetGraphicsState(
                          {.raster = {.cullMode = CullMode::None},
                           .depth = {.depthTestEnable = false, .depthWriteEnable = false},
                           .blend = {.enable = true, .destinationAlphaFactor = BlendFactor::OneMinusSourceAlpha}});

                      struct Parameters {
                          glm::vec2 inverseSize;
                          uint32_t vertices, texture, sampler, linearColor;
                      };
                      static_assert(sizeof(Parameters) == 24);

                      auto* vertexBuffer = context.GetBuffer(geometry);
                      const Parameters parameters{.inverseSize{1.0f / frame.width, 1.0f / frame.height},
                                                  .vertices = vertexBuffer->GetDescriptorIndex(),
                                                  .texture = m_Atlas->GetSampledImageDescriptorIndex(),
                                                  .sampler = m_Sampler->GetDescriptorIndex(),
                                                  .linearColor = linear};
                      commands.SetPushConstants(&parameters, sizeof(parameters));

                      commands.ClearResourceDeclarations();
                      commands.UseResource(vertexBuffer);
                      commands.UseResource(m_Atlas.Raw());
                      commands.UseResource(m_Sampler.Raw());

                      commands.Draw(count);

                      commands.EndRendering();
                  });

    return true;
}
