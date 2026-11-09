#include "rendering/Renderer.h"
#include "rendering/FrameResources.h"
#include "rendering/VoxelTextures.h"
#include "diagnostics/Profiling.h"
#include <iostream>

using namespace RealRHI;

Renderer::Renderer(Device& device, Swapchain& swapchain)
    : m_Device(device), m_Swapchain(swapchain), m_ChunkCache(device) {}

bool Renderer::Initialize() {
    auto& device = m_Device;
    MC_PROFILE_ZONE("Renderer initialization");
    const auto pixels = VoxelTextures::BuildAtlas();
    if (device.CreateTexture({
            .width = VoxelTextures::WIDTH, .height = VoxelTextures::HEIGHT,
            // Atlas bytes are display colors; decode them before lighting.
            .format = TextureFormat::RGBA8_SRGB,
            .usage = TextureUsage::ShaderResource | TextureUsage::TransferDst,
            .debugName = "VoxelAtlas",
        }, m_VoxelAtlas) != Result::Success ||
        m_VoxelAtlas->UploadData(pixels.data(), static_cast<uint32_t>(pixels.size())) != Result::Success ||
        device.CreateSampler({
            .minFilter = Filter::Nearest, .magFilter = Filter::Nearest, .mipFilter = Filter::Nearest,
            .addressU = SamplerAddressMode::ClampToEdge, .addressV = SamplerAddressMode::ClampToEdge,
            .debugName = "VoxelPixelSampler",
        }, m_VoxelSampler) != Result::Success) {
        std::cerr << "Failed to create voxel textures.\n";
        return false;
    }
    if (!CreateVoxelPipeline() || !CreateSkyPipeline() || !CreateShadowPipeline()) {
        return false;
    }

    m_Pool = std::make_unique<TransientResourcePool>(device, Options::Rendering::TRANSIENT_RESOURCE_POOL_BYTES);
    return true;
}

bool Renderer::CreateVoxelPipeline() {
    MC_PROFILE_ZONE("Create voxel pipelines");
    Ref<Shader> voxelShader;
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::TERRAIN_SHADER_MODULE,
        .debugName = "VoxelRasterShader",
    };
    if (m_Device.CreateShader(shaderDesc, voxelShader) != Result::Success) {
        std::cerr << "Failed to create voxel raster voxelShader.\n";
        return false;
    }

    PipelineDesc pipelineDesc{
        .shader = voxelShader.Raw(),
        .rasterState = { .cullMode = CullMode::Front, .fillMode = FillMode::Solid },
        .depthState = { .depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less },
        .renderTargetFormats = { .colorFormats = { m_Swapchain.GetBackBufferFormat() }, .depthFormat = TextureFormat::D32_Float },
        .debugName = "VoxelSolidPipeline",
    };
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_SolidPipeline) != Result::Success) {
        std::cerr << "Failed to create solid voxel pipeline.\n";
        return false;
    }
    pipelineDesc.rasterState.fillMode = FillMode::Wireframe;
    pipelineDesc.debugName = "VoxelWireframePipeline";
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_WireframePipeline) != Result::Success) {
        std::cerr << "Failed to create wireframe voxel pipeline.\n";
        return false;
    }

    return true;
}

bool Renderer::CreateShadowPipeline() {
    MC_PROFILE_ZONE("Create shadow pipeline");
    Ref<Shader> depthShader;
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::SHADOW_SHADER_MODULE,
        .debugName = "ShadowDepthShader",
    };
    if (m_Device.CreateShader(shaderDesc, depthShader) != Result::Success) {
        std::cerr << "Failed to create shadow depth voxelShader.\n";
        return false;
    }

    const PipelineDesc pipelineDesc{
        .shader = depthShader.Raw(),
        .rasterState = { .cullMode = CullMode::Front },
        .depthState = { .depthTestEnable = true, .depthWriteEnable = true, .compareOp = CompareOp::Less },
        .renderTargetFormats = { .depthFormat = TextureFormat::D32_Float },
        .debugName = "ShadowDepthPipeline",
    };
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_ShadowDepthPipeline) != Result::Success) {
        std::cerr << "Failed to create shadow depth pipeline.\n";
        return false;
    }

    constexpr SamplerDesc samplerDesc{
        // Bilinearly filter depth comparisons for each PCF tap, not the stored depths.
        .minFilter = Filter::Linear, .magFilter = Filter::Linear, .mipFilter = Filter::Nearest,
        .addressU = SamplerAddressMode::ClampToBorder, .addressV = SamplerAddressMode::ClampToBorder,
        .compareEnable = true, .compareOp = CompareOp::LessEqual, .borderColor = SamplerBorderColor::OpaqueWhite,
        .debugName = "ShadowComparisonSampler",
    };
    if (m_Device.CreateSampler(samplerDesc, m_ShadowComparisonSampler) != Result::Success) {
        std::cerr << "Failed to create shadow comparison sampler.\n";
        return false;
    }

    return true;
}

bool Renderer::CreateSkyPipeline() {
    MC_PROFILE_ZONE("Create sky pipeline");
    Ref<Shader> skyShader;
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::SKY_SHADER_MODULE,
        .debugName = "SkyShader",
    };
    if (m_Device.CreateShader(shaderDesc, skyShader) != Result::Success) {
        std::cerr << "Failed to create sky voxelShader.\n";
        return false;
    }

    PipelineDesc pipelineDesc{
        .shader = skyShader.Raw(),
        .rasterState = { .cullMode = CullMode::None },
        .depthState = { .depthTestEnable = false, .depthWriteEnable = false },
        .renderTargetFormats = { .colorFormats = { m_Swapchain.GetBackBufferFormat() }, .depthFormat = TextureFormat::D32_Float },
        .debugName = "SkyPipeline",
    };
    if (m_Device.CreateGraphicsPipeline(pipelineDesc, m_SkyPipeline) != Result::Success) {
        std::cerr << "Failed to create sky pipeline.\n";
        return false;
    }

    return true;
}

bool Renderer::DrawFrame(const FrameContext& frame, const Camera& camera, const World& world) {
    MC_PROFILE_ZONE("Draw frame");
    RenderGraph graph;
    FrameResources resources {
        .sunDirection = m_Sun.GetDirection(),
    };

    const ChunkCoord player = World::ChunkCoordinate(camera.GetPosition());
    {
        MC_PROFILE_ZONE("Build shadow cascades");
        resources.shadowMapData = ShadowMaps::BuildData(camera, resources.sunDirection, frame.width, frame.height);
    }
    CollectVisibleChunks(graph, resources, world, player, camera);
    AddShadowMapPasses(graph, resources);
    BufferDataInitPass(graph, resources);
    AddVoxelPass(graph, resources, frame, camera);

    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }

    return true;
}
