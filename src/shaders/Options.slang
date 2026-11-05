#pragma once

// The renderer reads the compiled workgroup size through shader reflection.
#define VOXEL_WORKGROUP_WIDTH 16
#define VOXEL_WORKGROUP_HEIGHT 16

// Must hold the root plus Options::World::OCTREE_DEPTH descendant levels.
#define VOXEL_OCTREE_STACK_SIZE 32
#define VOXEL_HEATMAP_MAX_STEPS 128.0f
#define VOXEL_MAX_RAY_DISTANCE 3.402823466e+38f

// CPU/GPU data contract: keep in sync with OctreeFormat and MISSING_CHUNK in Options.h.
#define VOXEL_CHILD_MASK 0xFFu
#define VOXEL_PAYLOAD_SHIFT 8u
#define VOXEL_MISSING_CHUNK 0xFFFFFFFFu
