#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

// Compile-time settings: edit here and rebuild. Derived values update automatically.
namespace Options {
namespace World {
	inline constexpr uint32_t OCTREE_DEPTH = 6; // 64*64*64 volume chunk.
    inline constexpr uint32_t CHUNK_SIZE = 1u << OCTREE_DEPTH;

    inline constexpr int MESH_RADIUS = 8;
    // Keep a voxel-only border available for meshing the visible volume.
    inline constexpr int LOAD_RADIUS = MESH_RADIUS + 1;
    inline constexpr int UNLOAD_RADIUS = LOAD_RADIUS + 1;

    inline constexpr uint32_t CHUNKS_LOADED_PER_FRAME = 5;

    inline constexpr int TERRAIN_SEED = 1337;
    inline constexpr float TERRAIN_FEATURE_SCALE = 128.0f; // Width of broad hills in voxels.
    inline constexpr int TERRAIN_OCTAVES = 4;
    inline constexpr int TERRAIN_BASE_HEIGHT = 24;
    inline constexpr int TERRAIN_HEIGHT_AMPLITUDE = 20;
    inline constexpr int TERRAIN_DIRT_DEPTH = 3; // Dirt blocks below the grass surface.

    static_assert(OCTREE_DEPTH < 31);
    static_assert(MESH_RADIUS >= 0 && LOAD_RADIUS >= MESH_RADIUS + 1 && UNLOAD_RADIUS > LOAD_RADIUS);
    static_assert(CHUNKS_LOADED_PER_FRAME > 0);
    static_assert(TERRAIN_FEATURE_SCALE > 0 && TERRAIN_OCTAVES > 0);
    static_assert(TERRAIN_HEIGHT_AMPLITUDE >= 0 && TERRAIN_DIRT_DEPTH >= 0);
}
namespace Rendering {
    inline constexpr uint64_t TRANSIENT_POOL_BYTES = 256ull * 1024 * 1024;
    inline constexpr const char* SHADER_MODULE = "raster.slang";
    inline constexpr const char* SKY_SHADER_MODULE = "sky.slang";
    inline constexpr glm::vec3 SUN_DIRECTION = {0.4f, 0.85f, 0.3f}; // Initial direction toward the sun.
    inline constexpr float SUN_ANGULAR_SIZE_DEGREES = 6.0f;
    inline constexpr double SUN_ORBIT_PERIOD_SECONDS = 60.0;
    
    // Ray-traced sun shadows
    inline constexpr uint32_t VOXEL_SHADOW_CHUNKS_PER_SIDE = 3; // Must match the current 3x3x3 chunk-neighbor input.
    inline constexpr uint32_t VOXEL_SHADOW_BRICK_SIZE = 8; // VOXEL_SHADOW_BRICK_SIZExVOXEL_SHADOW_BRICK_SIZExVOXEL_SHADOW_BRICK_SIZE
    // Fade is non-linear smoothstep
    inline constexpr float VOXEL_SHADOW_FADE_START_DISTANCE = 36.0f;
    inline constexpr float VOXEL_SHADOW_FADE_END_DISTANCE = 48.0f;
    inline constexpr float VOXEL_SHADOW_MAX_RAY_LENGTH = 64.0f;
    
    // Camera
    inline constexpr float NEAR_PLANE = 0.1f;
    inline constexpr float FAR_PLANE = (World::UNLOAD_RADIUS + 2) * World::CHUNK_SIZE * 2.0f;

    // Fog
    inline constexpr float FOG_END = (World::MESH_RADIUS > 0 ? World::MESH_RADIUS : 1) * World::CHUNK_SIZE * 0.9f;
    inline constexpr float FOG_START = FOG_END * 0.5f;

    static_assert(NEAR_PLANE > 0 && FAR_PLANE > NEAR_PLANE);
    static_assert(FOG_START >= 0 && FOG_END > FOG_START && FOG_END < FAR_PLANE);
    static_assert(SUN_ANGULAR_SIZE_DEGREES > 0 && SUN_ANGULAR_SIZE_DEGREES < 180);
    static_assert(SUN_ORBIT_PERIOD_SECONDS > 0);
    static_assert(VOXEL_SHADOW_CHUNKS_PER_SIDE == 3, "Shadow volumes currently require 3x3x3 chunk neighbors.");
    static_assert(VOXEL_SHADOW_BRICK_SIZE > 0 && World::CHUNK_SIZE % VOXEL_SHADOW_BRICK_SIZE == 0);
    static_assert(VOXEL_SHADOW_FADE_START_DISTANCE >= 0 && VOXEL_SHADOW_FADE_END_DISTANCE > VOXEL_SHADOW_FADE_START_DISTANCE);
    static_assert(VOXEL_SHADOW_MAX_RAY_LENGTH > 0);
}
namespace Camera {
    inline constexpr glm::vec3 START_POSITION = {
        0.0f, float(World::TERRAIN_BASE_HEIGHT + World::TERRAIN_HEIGHT_AMPLITUDE + 8), -12.0f};
    inline constexpr float START_YAW_DEGREES = 90.0f;
    inline constexpr float START_PITCH_DEGREES = -20.0f;
    inline constexpr float FOV_Y_DEGREES = 70.0f;
    inline constexpr float PITCH_LIMIT_MARGIN_RADIANS = 0.01f;
    inline constexpr float MOVE_SPEED = 32.0f; // World units per second.
    inline constexpr float TURN_SPEED = 1.8f; // Radians per second.

    static_assert(FOV_Y_DEGREES > 0 && FOV_Y_DEGREES < 180);
}
namespace Application {
    inline constexpr const char* NAME = "Minecraft Clone";
    inline constexpr const char* SHADER_DIRECTORY = "shaders";
    inline constexpr uint32_t WINDOW_WIDTH = 800;
    inline constexpr uint32_t WINDOW_HEIGHT = 600;
    inline constexpr bool ENABLE_DEBUG = true;
    inline constexpr uint32_t MINIMIZED_DELAY_MS = 16;
}
}
