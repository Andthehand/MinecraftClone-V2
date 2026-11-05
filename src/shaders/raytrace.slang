#include "Options.slang"

struct ComputeParameters {
    uint width;
    uint height;
    uint chunkSize;
    uint chunkTableIndex;

    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;

    float tanHalfFovY;
    float aspect;
    uint chunkBufferIndex;
    uint outputTextureIndex;
    int4 gridOrigin;
    uint4 gridSize;
};

[[vk::push_constant]]
ConstantBuffer<ComputeParameters> parameters;

struct Ray {
    float3 origin;
    float3 direction;
};

Ray GeneratePrimaryRay(
    uint2 pixel,
    uint width,
    uint height,
    float3 cameraPosition,
    float3 cameraForward,
    float3 cameraRight,
    float3 cameraUp,
    float tanHalfFovY,
    float aspect) {
    float2 resolution = float2((float)width, (float)height);
    float2 uv = (float2(pixel) + 0.5f) / resolution;
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float3 direction = normalize(
        cameraForward +
        cameraRight * (ndc.x * aspect * tanHalfFovY) +
        cameraUp * (ndc.y * tanHalfFovY));

    return Ray(cameraPosition, direction);
}

// Handles parallel rays without dividing by zero.
// Parallel rays use half-open bounds at subdivision planes.
bool IntersectAabb(
    Ray ray,
    float3 boundsMin,
    float3 boundsMax,
    out float tEnter,
    out float tExit)
{
    tEnter = 0.0f;
    tExit = VOXEL_MAX_RAY_DISTANCE;

    for (uint axis = 0u; axis < 3u; ++axis) {
        float origin = ray.origin[axis];
        float direction = ray.direction[axis];

        if (direction == 0.0f) {
            if (origin < boundsMin[axis] ||
                origin >= boundsMax[axis])
            {
                return false;
            }
        } else {
            float a = (boundsMin[axis] - origin) / direction;
            float b = (boundsMax[axis] - origin) / direction;

            tEnter = max(tEnter, min(a, b));
            tExit = min(tExit, max(a, b));

            if (tExit <= tEnter)
                return false;
        }
    }

    return tExit > tEnter;
}

struct TraversalFrame {
    uint packed;
    uint remainingMask;
    float3 boundsMin;
    float size;
    float tEnter;
};

// Root plus 31 descendant levels.
static const uint OCTREE_STACK_SIZE = VOXEL_OCTREE_STACK_SIZE;

// Lower values make the heatmap turn red/yellow sooner.
static const float HEATMAP_MAX_STEPS = VOXEL_HEATMAP_MAX_STEPS;

float3 TraversalHeatmap(uint steps) {
    float heat = saturate(
        (float)steps / HEATMAP_MAX_STEPS);

    // Black -> red -> yellow.
    return float3(
        saturate(heat * 2.0f),
        saturate(heat * 2.0f - 1.0f),
        0.0f);
}

bool TraceOctree(
    StructuredBuffer<uint> nodes,
    Ray ray,
    uint chunkSize,
    uint rootIndex,
    out uint voxelId,
    out int3 voxel,
    out float tHit,
    out uint traversalSteps)
{
    traversalSteps = 0u;
    voxelId = 0u;
    voxel = int3(0, 0, 0);
    tHit = 0.0f;

    if (chunkSize == 0u)
        return false;

    float rootEnter;
    float rootExit;

    if (!IntersectAabb(
        ray,
        float3(0.0f),
        float3((float)chunkSize),
        rootEnter,
        rootExit))
    {
        return false;
    }

    TraversalFrame stack[OCTREE_STACK_SIZE];
    uint stackSize = 1u;

    uint root = nodes[rootIndex];

    stack[0].packed = root;
    stack[0].remainingMask = root & VOXEL_CHILD_MASK;
    stack[0].boundsMin = float3(0.0f);
    stack[0].size = (float)chunkSize;
    stack[0].tEnter = rootEnter;

    while (stackSize != 0u) {
        // Counts loop iterations, including returning to a parent.
        ++traversalSteps;

        uint top = stackSize - 1u;
        TraversalFrame frame = stack[top];

        uint childMask = frame.packed & VOXEL_CHILD_MASK;
        uint payload = frame.packed >> VOXEL_PAYLOAD_SHIFT;

        // Leaf: payload is the voxel ID.
        if (childMask == 0u) {
            --stackSize;

            if (payload == 0u)
                continue;

            voxelId = payload;
            tHit = frame.tEnter;

            // Occupied leaves from the CPU implementation are
            // one voxel wide. Clamp at maximum-face intersections.
            float3 local = clamp(
                floor(
                    ray.origin +
                    ray.direction * tHit -
                    frame.boundsMin),
                float3(0.0f),
                float3(max(frame.size - 1.0f, 0.0f)));

            voxel = int3(frame.boundsMin + local);
            return true;
        }

        if (frame.remainingMask == 0u) {
            --stackSize;
            continue;
        }

        // Find the nearest intersected child still awaiting traversal.
        float halfSize = frame.size * 0.5f;

        uint bestChild = 8;
        float bestEnter = VOXEL_MAX_RAY_DISTANCE;
        float3 bestMin = float3(0.0f);
        uint intersectedMask = 0u;

        for (uint child = 0u; child < 8; ++child) {
            uint bit = 1u << child;

            if ((frame.remainingMask & bit) == 0u)
                continue;

            // Matches the CPU child numbering: x + 2*y + 4*z.
            float3 childMin = frame.boundsMin + halfSize * float3(
                (float)(child & 1u),
                (float)((child >> 1u) & 1u),
                (float)((child >> 2u) & 1u));

            float childEnter;
            float childExit;

            if (!IntersectAabb(
                ray,
                childMin,
                childMin + float3(halfSize),
                childEnter,
                childExit))
            {
                continue;
            }

            intersectedMask |= bit;

            if (childEnter < bestEnter) {
                bestChild = child;
                bestEnter = childEnter;
                bestMin = childMin;
            }
        }

        if (bestChild == 8) {
            --stackSize;
            continue;
        }

        // Preserve other intersected siblings for later traversal.
        stack[top].remainingMask =
            intersectedMask & ~(1u << bestChild);

        // Calculate the packed child index using the ORIGINAL mask.
        uint precedingMask =
            childMask & ((1u << bestChild) - 1u);

        uint childIndex = payload + countbits(precedingMask);
        uint childPacked = nodes[rootIndex + childIndex];

        // Valid Octree<depth> output with depth <= 31 fits the stack.
        if (stackSize >= OCTREE_STACK_SIZE)
            return false;

        stack[stackSize].packed = childPacked;
        stack[stackSize].remainingMask = childPacked & VOXEL_CHILD_MASK;
        stack[stackSize].boundsMin = bestMin;
        stack[stackSize].size = halfSize;
        stack[stackSize].tEnter = bestEnter;

        ++stackSize;
    }

    return false;
}

// Walk chunks in ray order, then traverse each local octree at its pool allocation.
bool TraceWorld(StructuredBuffer<uint> nodes, StructuredBuffer<uint> chunks,
                Ray ray, out uint traversalSteps) {
    traversalSteps = 0u;
    float chunkSize = (float)parameters.chunkSize;
    float3 worldMin = float3(parameters.gridOrigin.xyz) * chunkSize;
    float3 worldMax = worldMin + float3(parameters.gridSize.xyz) * chunkSize;
    float enter, exit;
    if (!IntersectAabb(ray, worldMin, worldMax, enter, exit))
        return false;

    float3 cellPosition = (ray.origin + ray.direction * enter - worldMin) / chunkSize;
    int3 cell;
    int3 step;
    float3 nextBoundary;
    float3 delta;
    for (uint axis = 0u; axis < 3u; ++axis) {
        float direction = ray.direction[axis];
        step[axis] = direction > 0.0f ? 1 : (direction < 0.0f ? -1 : 0);
        // A ray on a boundary belongs to the cell it is entering.
        cell[axis] = direction < 0.0f ? (int)ceil(cellPosition[axis]) - 1 : (int)floor(cellPosition[axis]);
        cell[axis] = clamp(cell[axis], 0, (int)parameters.gridSize[axis] - 1);
        if (direction == 0.0f) {
            nextBoundary[axis] = VOXEL_MAX_RAY_DISTANCE;
            delta[axis] = VOXEL_MAX_RAY_DISTANCE;
        } else {
            float boundary = worldMin[axis] +
                (float)(cell[axis] + (step[axis] > 0 ? 1 : 0)) * chunkSize;
            nextBoundary[axis] = max(enter, (boundary - ray.origin[axis]) / direction);
            delta[axis] = chunkSize / abs(direction);
        }
    }

    uint maxSteps = parameters.gridSize.x + parameters.gridSize.y + parameters.gridSize.z;
    for (uint i = 0u; i < maxSteps; ++i) {
        uint index = (uint)cell.x + parameters.gridSize.x *
            ((uint)cell.y + parameters.gridSize.y * (uint)cell.z);
        uint rootIndex = chunks[index];
        ++traversalSteps;
        if (rootIndex != VOXEL_MISSING_CHUNK) {
            float3 chunkMin = worldMin + float3(cell) * chunkSize;
            Ray localRay = Ray(ray.origin - chunkMin, ray.direction);
            uint voxelId, steps;
            int3 voxel;
            float tHit;
            bool hit = TraceOctree(nodes, localRay, parameters.chunkSize, rootIndex,
                                   voxelId, voxel, tHit, steps);
            traversalSteps += steps;
            if (hit)
                return true;
        }

        float next = min(nextBoundary.x, min(nextBoundary.y, nextBoundary.z));
        if (next >= exit)
            break;
        // Advance all tied axes at edges/corners, avoiding zero-length cells.
        for (uint axis = 0u; axis < 3u; ++axis) {
            if (nextBoundary[axis] <= next) {
                cell[axis] += step[axis];
                nextBoundary[axis] += delta[axis];
            }
        }
        if (any(cell < int3(0)) || any(cell >= int3(parameters.gridSize.xyz)))
            break;
    }
    return false;
}
[require(spvDescriptorHeapEXT)]
[shader("compute")]
[numthreads(VOXEL_WORKGROUP_WIDTH, VOXEL_WORKGROUP_HEIGHT, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID) {
    if (threadId.x >= parameters.width ||
        threadId.y >= parameters.height)
    {
        return;
    }

    uint2 pixel = threadId.xy;

    StructuredBuffer<uint> nodes =
        StructuredBuffer<uint>.Handle(
            uint2(parameters.chunkBufferIndex, 0));

    StructuredBuffer<uint> chunks = StructuredBuffer<uint>.Handle(
        uint2(parameters.chunkTableIndex, 0));

    RWTexture2D<float4> outputTexture =
        RWTexture2D<float4>.Handle(
            uint2(parameters.outputTextureIndex, 0));

    Ray ray = GeneratePrimaryRay(
        pixel,
        parameters.width,
        parameters.height,
        parameters.cameraPosition.xyz,
        parameters.cameraForward.xyz,
        parameters.cameraRight.xyz,
        parameters.cameraUp.xyz,
        parameters.tanHalfFovY,
        parameters.aspect);

    uint traversalSteps;
    TraceWorld(nodes, chunks, ray, traversalSteps);

    // Shade misses too: a ray can traverse many nodes without a hit.
    float3 color = TraversalHeatmap(traversalSteps);

    outputTexture[pixel] = float4(color, 1.0f);
}
