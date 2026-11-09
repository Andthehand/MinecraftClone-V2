#include "rendering/FrameResources.h"
#include "diagnostics/Profiling.h"
#include <algorithm>

using namespace RealRHI;

void Renderer::CollectVisibleChunks(RenderGraph& graph, FrameResources& resources, const World& world, ChunkCoord player,
    const Camera& camera) {
    MC_PROFILE_ZONE("Collect visible chunks");
    const auto planes = camera.GetFrustumPlanes();

    // Since we are doing 3 cascaded shadow maps, we need to check visibility against the light frustum as well.
    std::array<std::array<glm::vec4, 6>, 3> lightPlanes;
    for (uint32_t cascade = 0; cascade < lightPlanes.size(); ++cascade) {
        lightPlanes[cascade] = ShadowMaps::MatrixPlanes(resources.shadowMapData.lightMatrices[cascade]);
    }

    auto& uses = resources.geometryResourceUses;
    for (auto& [coord, chunk] : m_ChunkCache.GetMeshes()) {
        if (chunk.indexCount == 0 || !world.CanMeshChunk(coord, player)) {
            continue;
        }

        const glm::vec3 chunkMinimum = glm::vec3(coord) * float(Options::World::CHUNK_SIZE);
        const auto chunkMaximum = chunkMinimum + glm::vec3(float(Options::World::CHUNK_SIZE));

        const bool cameraVisible = Camera::IsBoxVisible(planes, chunkMinimum, chunkMaximum);

        uint32_t cascadeMask = 0;
        for (uint32_t cascade = 0; cascade < lightPlanes.size(); ++cascade) {
            if (Camera::IsBoxVisible(lightPlanes[cascade], chunkMinimum, chunkMaximum)) {
                cascadeMask |= 1u << cascade;
            }
        }

        if (!cameraVisible && cascadeMask == 0) {
            // Not visible to camera or any shadow cascade, skip...
            continue;
        }

        if (cameraVisible) {
            resources.visibleChunks.push_back({ coord, &chunk });
        }
        if (cascadeMask != 0) {
            resources.shadowVisibleChunks.push_back({ coord, &chunk, cascadeMask });
        }

        uses.push_back({graph.ImportBuffer(chunk.vertices.Raw()),
            {PipelineStage::VertexShader, ResourceAccess::ShaderRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
        uses.push_back({graph.ImportBuffer(chunk.indices.Raw()),
            {PipelineStage::VertexInput, ResourceAccess::IndexRead}, TextureLayout::Undefined, RenderGraphAccess::Read});
    }

    // Near chunks populate depth first, reducing shading of hidden surfaces.
    const glm::vec3 cameraPosition = camera.GetPosition();
    const auto distanceSquared = [cameraPosition](ChunkCoord coord) {
        const glm::vec3 center = (glm::vec3(coord) + glm::vec3(0.5f)) * float(Options::World::CHUNK_SIZE);
        const glm::vec3 delta = center - cameraPosition;
        return glm::dot(delta, delta);
    };
    std::sort(resources.visibleChunks.begin(), resources.visibleChunks.end(),
        [&distanceSquared](const auto& a, const auto& b) {
            return distanceSquared(a.coord) < distanceSquared(b.coord);
        }
    );
    MC_PROFILE_PLOT("Visible chunks", static_cast<int64_t>(resources.visibleChunks.size()));
    MC_PROFILE_PLOT("Shadow chunks", static_cast<int64_t>(resources.shadowVisibleChunks.size()));
}

