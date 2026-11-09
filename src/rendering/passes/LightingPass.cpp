#include "rendering/FrameResources.h"
#include "rendering/ShaderData.h"
#include "diagnostics/Profiling.h"
#include <glm/matrix.hpp>
#include <iostream>
#include <string>

using namespace RealRHI;
using namespace ShaderData;

void Renderer::BufferDataInitPass(RenderGraph& graph, FrameResources& resources) {
    MC_PROFILE_ZONE("Build lighting pass");
    const SunData sunData{.sunDirection = glm::vec4(resources.sunDirection, 0)};
    resources.sunBuffer = graph.CreateBuffer({
        .size = sizeof(SunData), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .debugName = "SunLightingData",
    });

	constexpr ResourceState sunDataWrite{ PipelineStage::AllCommands, ResourceAccess::MemoryWrite };    
    graph.AddPass({
        .name = "InitializeBufferData",
        .resources = {{resources.sunBuffer, sunDataWrite, TextureLayout::Undefined, RenderGraphAccess::Write},
    }}, [&graph, &resources, sunData](CommandList& commands) {
        auto* buffer = graph.GetBuffer(resources.sunBuffer);
        commands.UseResource(buffer);

        auto data = sunData;
        data.shadowDataIndex = graph.GetBuffer(resources.shadowDataBuffer)->GetDescriptorIndex();
        if (buffer->WriteData(&data, sizeof(data)) != Result::Success) {
            std::cerr << "Failed to write sun lighting data.\n";
        }
    });
}

