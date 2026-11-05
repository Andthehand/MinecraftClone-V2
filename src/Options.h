#pragma once

#include <cstdint>
#include <numbers>

// Compile-time settings: edit here and rebuild. Derived values update automatically.
namespace Options {
namespace World {
	inline constexpr uint32_t OCTREE_DEPTH = 5; // 32*32*32 volume chunk.
    inline constexpr uint32_t CHUNK_SIZE = 1u << OCTREE_DEPTH;

    inline constexpr int LOAD_RADIUS = 3;
    inline constexpr int UNLOAD_RADIUS = LOAD_RADIUS + 1;
    inline constexpr uint32_t GRID_SIZE = 2 * UNLOAD_RADIUS + 1;
    inline constexpr uint32_t GRID_CELLS = GRID_SIZE * GRID_SIZE * GRID_SIZE;

    // Capacity in nodes, not bytes: 42 million (binary) four-byte nodes = 168 MiB.
    inline constexpr uint32_t NODE_POOL_CAPACITY = 42u * 1024u * 1024u;
    inline constexpr uint32_t CHUNKS_LOADED_PER_FRAME = 4;
    inline constexpr uint32_t MISSING_CHUNK = UINT32_MAX;

	// TODO: Remove when debug sphere is no longer used.
    inline constexpr float SPHERE_RADIUS_RATIO = 0.45f;
    inline constexpr uint32_t SPHERE_VOXEL_ID = 1;

    static_assert(OCTREE_DEPTH < 31);
    static_assert(LOAD_RADIUS >= 0 && UNLOAD_RADIUS > LOAD_RADIUS);
    static_assert(CHUNKS_LOADED_PER_FRAME > 0 && NODE_POOL_CAPACITY > 0);
}
namespace Rendering {
    inline constexpr uint64_t TRANSIENT_POOL_BYTES = 64ull * 1024 * 1024;
    inline constexpr uint64_t MIN_NODE_ALLOCATION_ALIGNMENT_BYTES = 256;
    inline constexpr const char* SHADER_MODULE = "raytrace.slang";

    static_assert(MIN_NODE_ALLOCATION_ALIGNMENT_BYTES > 0 &&
        (MIN_NODE_ALLOCATION_ALIGNMENT_BYTES & (MIN_NODE_ALLOCATION_ALIGNMENT_BYTES - 1)) == 0);
}
namespace Camera {
    inline constexpr float START_POSITION[3] = {0.0f, 8.0f, -12.0f};
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
namespace Math {
    inline constexpr float PI = std::numbers::pi_v<float>;
    inline constexpr float HALF_PI = PI * 0.5f;
    inline constexpr float DEGREES_TO_RADIANS = PI / 180.0f;
}
// This encoding must match shaders/Options.slang.
namespace OctreeFormat {
    inline constexpr uint32_t CHILD_MASK = 0xFFu;
    inline constexpr uint32_t PAYLOAD_SHIFT = 8u;
}
namespace Hashing {
    inline constexpr uint32_t COMBINE_SEED = 0x9e3779b9u;
    inline constexpr uint32_t LEFT_SHIFT = 6;
    inline constexpr uint32_t RIGHT_SHIFT = 2;
}
}
