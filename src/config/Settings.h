#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

// Compile-time settings: edit here and rebuild. Shadow settings are uploaded
// to the shader with the per-frame cascade data.
namespace Options {
namespace World {
    inline constexpr uint32_t CHUNK_OCTREE_DEPTH = 6; // 64 voxels along each chunk edge.
    inline constexpr uint32_t CHUNK_SIZE = 1u << CHUNK_OCTREE_DEPTH;

    inline constexpr int MESH_RADIUS_CHUNKS = 8;
    // Keep a voxel-only border available for meshing the visible volume.
    inline constexpr int LOAD_RADIUS_CHUNKS = MESH_RADIUS_CHUNKS + 1;
    inline constexpr int UNLOAD_RADIUS_CHUNKS = LOAD_RADIUS_CHUNKS + 1;

    inline constexpr uint32_t MAX_CHUNKS_GENERATED_PER_FRAME = 5; // Budget for newly generated chunks.

    inline constexpr int TERRAIN_SEED = 1337;
    inline constexpr float TERRAIN_FEATURE_SCALE = 128.0f; // Width of broad hills in voxels.
    inline constexpr int TERRAIN_OCTAVES = 4;
    inline constexpr int TERRAIN_BASE_HEIGHT = 24;
    inline constexpr int TERRAIN_HEIGHT_AMPLITUDE = 20;
    inline constexpr int TERRAIN_DIRT_DEPTH = 3; // Dirt blocks below the grass surface.

    static_assert(CHUNK_OCTREE_DEPTH < 31);
    static_assert(MESH_RADIUS_CHUNKS >= 0 && LOAD_RADIUS_CHUNKS >= MESH_RADIUS_CHUNKS + 1 && UNLOAD_RADIUS_CHUNKS > LOAD_RADIUS_CHUNKS);
    static_assert(MAX_CHUNKS_GENERATED_PER_FRAME > 0);
    static_assert(TERRAIN_FEATURE_SCALE > 0 && TERRAIN_OCTAVES > 0);
    static_assert(TERRAIN_HEIGHT_AMPLITUDE >= 0 && TERRAIN_DIRT_DEPTH >= 0);
}
namespace Rendering {
    inline constexpr uint64_t TRANSIENT_RESOURCE_POOL_BYTES = 256ull * 1024 * 1024;
    inline constexpr const char* TERRAIN_SHADER_MODULE = "raster.slang";
    inline constexpr const char* SKY_SHADER_MODULE = "sky.slang";
    inline constexpr const char* SHADOW_SHADER_MODULE = "shadow_depth.slang";
    inline constexpr glm::vec3 INITIAL_SUN_DIRECTION = {0.4f, 0.85f, 0.3f}; // Initial direction toward the sun.
    inline constexpr float SUN_ANGULAR_DIAMETER_DEGREES = 6.0f;
    inline constexpr double SUN_ORBIT_PERIOD_SECONDS = 12000.0;

    // Three cascades, with an overlap fraction before each split.
    inline constexpr uint32_t SHADOW_MAP_RESOLUTION = 2048;
    inline constexpr float SHADOW_FADE_START_DISTANCE = 36.0f;
    inline constexpr float SHADOW_FADE_END_DISTANCE = 48.0f;
    inline constexpr float SHADOW_CASTER_DEPTH_EXTENSION = 64.0f; // Extra world-space depth toward the sun for off-camera casters.
    inline constexpr float SHADOW_CASCADE_OVERLAP_FRACTION = 0.10f;

    // Fog
    inline constexpr float FOG_END_DISTANCE = (World::MESH_RADIUS_CHUNKS > 0 ? World::MESH_RADIUS_CHUNKS : 1) * World::CHUNK_SIZE * 0.9f;
    inline constexpr float FOG_START_DISTANCE = FOG_END_DISTANCE * 0.5f;

    static_assert(FOG_START_DISTANCE >= 0 && FOG_END_DISTANCE > FOG_START_DISTANCE);
    static_assert(SUN_ANGULAR_DIAMETER_DEGREES > 0 && SUN_ANGULAR_DIAMETER_DEGREES < 180);
    static_assert(SUN_ORBIT_PERIOD_SECONDS > 0);
    static_assert(SHADOW_MAP_RESOLUTION > 0);
    static_assert(SHADOW_FADE_START_DISTANCE >= 0 && SHADOW_FADE_END_DISTANCE > SHADOW_FADE_START_DISTANCE);
    static_assert(SHADOW_CASTER_DEPTH_EXTENSION > 0);
    static_assert(SHADOW_CASCADE_OVERLAP_FRACTION > 0 && SHADOW_CASCADE_OVERLAP_FRACTION < 1);
}
namespace Camera {
    inline constexpr float NEAR_CLIP_DISTANCE = 0.1f;
    inline constexpr float FAR_CLIP_DISTANCE = (World::UNLOAD_RADIUS_CHUNKS + 2) * World::CHUNK_SIZE * 2.0f;
    inline constexpr glm::vec3 START_POSITION = {
        0.0f, float(World::TERRAIN_BASE_HEIGHT + World::TERRAIN_HEIGHT_AMPLITUDE + 8), -12.0f};
    inline constexpr float START_YAW_DEGREES = 90.0f;
    inline constexpr float START_PITCH_DEGREES = -20.0f;
    inline constexpr float VERTICAL_FOV_DEGREES = 70.0f;
    inline constexpr float PITCH_LIMIT_MARGIN_RADIANS = 0.01f;
    inline constexpr float MOVE_SPEED_UNITS_PER_SECOND = 32.0f; // World units per second.
    inline constexpr float TURN_SPEED_RADIANS_PER_SECOND = 1.8f; // Radians per second.

    static_assert(NEAR_CLIP_DISTANCE > 0 && FAR_CLIP_DISTANCE > NEAR_CLIP_DISTANCE);
    static_assert(Rendering::FOG_END_DISTANCE < FAR_CLIP_DISTANCE);
    static_assert(VERTICAL_FOV_DEGREES > 0 && VERTICAL_FOV_DEGREES < 180);
}
namespace Application {
    inline constexpr const char* NAME = "Minecraft Clone";
    inline constexpr const char* SHADER_DIRECTORY = "shaders";
    inline constexpr uint32_t WINDOW_WIDTH = 800;
    inline constexpr uint32_t WINDOW_HEIGHT = 600;
    inline constexpr bool ENABLE_GRAPHICS_DEBUG = false;
    inline constexpr bool ENABLE_VSYNC = true;
    inline constexpr uint32_t MINIMIZED_FRAME_DELAY_MS = 16;
}
}
