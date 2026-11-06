#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

// Compile-time settings: edit here and rebuild. Derived values update automatically.
namespace Options {
namespace World {
	inline constexpr uint32_t OCTREE_DEPTH = 5; // 32*32*32 volume chunk.
    inline constexpr uint32_t CHUNK_SIZE = 1u << OCTREE_DEPTH;

    inline constexpr int LOAD_RADIUS = 3;
    inline constexpr int UNLOAD_RADIUS = LOAD_RADIUS + 1;

    inline constexpr uint32_t CHUNKS_LOADED_PER_FRAME = 4;

	// TODO: Remove when debug sphere is no longer used.
    inline constexpr float SPHERE_RADIUS_RATIO = 0.45f;
    inline constexpr uint32_t SPHERE_VOXEL_ID = 1;

    static_assert(OCTREE_DEPTH < 31);
    static_assert(LOAD_RADIUS >= 0 && UNLOAD_RADIUS > LOAD_RADIUS);
    static_assert(CHUNKS_LOADED_PER_FRAME > 0);
}
namespace Rendering {
    inline constexpr uint64_t TRANSIENT_POOL_BYTES = 64ull * 1024 * 1024;
    inline constexpr const char* SHADER_MODULE = "raster.slang";
    inline constexpr float NEAR_PLANE = 0.1f;
    inline constexpr float FAR_PLANE = (World::UNLOAD_RADIUS + 2) * World::CHUNK_SIZE * 2.0f;

    static_assert(NEAR_PLANE > 0 && FAR_PLANE > NEAR_PLANE);
}
namespace Camera {
    inline constexpr glm::vec3 START_POSITION = {0.0f, 8.0f, -12.0f};
    inline constexpr float START_YAW_DEGREES = 90.0f;
    inline constexpr float START_PITCH_DEGREES = 0.0f;
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
