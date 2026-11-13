#include "rendering/Renderer.h"
#include "rendering/FrameResources.h"
#include "rendering/VoxelTextures.h"
#include "diagnostics/Profiling.h"
#include "diagnostics/DebugUI.h"
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
    if (!CreateVoxelShader() || !CreateSkyShader() || !CreateShadowShader()) {
        return false;
    }

    m_Pool = std::make_unique<TransientResourcePool>(device, Options::Rendering::TRANSIENT_RESOURCE_POOL_BYTES);
    return true;
}

bool Renderer::CreateVoxelShader() {
    MC_PROFILE_ZONE("Create voxel shader");
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::TERRAIN_SHADER_MODULE,
        .debugName = "VoxelRasterShader",
    };
    if (m_Device.CreateShader(shaderDesc, m_VoxelShader) != Result::Success) {
        std::cerr << "Failed to create voxel raster shader.\n";
        return false;
    }

    return true;
}

bool Renderer::CreateShadowShader() {
    MC_PROFILE_ZONE("Create shadow shader");
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::SHADOW_SHADER_MODULE,
        .debugName = "ShadowDepthShader",
    };
    if (m_Device.CreateShader(shaderDesc, m_ShadowDepthShader) != Result::Success) {
        std::cerr << "Failed to create shadow depth shader.\n";
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

bool Renderer::CreateSkyShader() {
    MC_PROFILE_ZONE("Create sky shader");
    const ShaderDesc shaderDesc{
        .moduleName = Options::Rendering::SKY_SHADER_MODULE,
        .debugName = "SkyShader",
    };
    if (m_Device.CreateShader(shaderDesc, m_SkyShader) != Result::Success) {
        std::cerr << "Failed to create sky shader.\n";
        return false;
    }

    return true;
}

bool Renderer::DrawFrame(const FrameContext& frame, const Camera& camera, const World& world, DebugUI& ui) {
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
    const auto backbuffer = graph.ImportBackBuffer(m_Swapchain, frame);
    AddVoxelPass(graph, resources, frame, camera, backbuffer);
    if (!ui.AddRenderPass(graph, backbuffer, frame, m_Swapchain.GetBackBufferFormat())) {
        std::cerr << "Failed to prepare debug UI rendering.\n";
        return false;
    }

    RenderGraphSubmission submission;
    if (graph.Submit(m_Device, submission, m_Pool.get()) != Result::Success) {
        std::cerr << "Raster render graph submission failed.\n";
        return false;
    }
    if (!ui.RenderSucceeded()) {
        std::cerr << "Debug UI rendering failed.\n";
        return false;
    }
    return true;
}
