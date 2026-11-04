struct ComputeParameters {
    uint width;
    uint height;
    uint chunkSize;
    uint padding;

    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;

    float tanHalfFovY;
    float aspect;
    uint chunkBufferIndex;
    uint outputTextureIndex;
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
    tExit = 3.402823466e+38f;

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
static const uint OCTREE_STACK_SIZE = 32u;

// Lower values make the heatmap turn red/yellow sooner.
static const float HEATMAP_MAX_STEPS = 64.0f;

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

    uint root = nodes[0];

    stack[0].packed = root;
    stack[0].remainingMask = root & 0xFFu;
    stack[0].boundsMin = float3(0.0f);
    stack[0].size = (float)chunkSize;
    stack[0].tEnter = rootEnter;

    while (stackSize != 0u) {
        // Counts loop iterations, including returning to a parent.
        ++traversalSteps;

        uint top = stackSize - 1u;
        TraversalFrame frame = stack[top];

        uint childMask = frame.packed & 0xFFu;
        uint payload = frame.packed >> 8u;

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

        uint bestChild = 8u;
        float bestEnter = 3.402823466e+38f;
        float3 bestMin = float3(0.0f);
        uint intersectedMask = 0u;

        for (uint child = 0u; child < 8u; ++child) {
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

        if (bestChild == 8u) {
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
        uint childPacked = nodes[childIndex];

        // Valid Octree<depth> output with depth <= 31 fits the stack.
        if (stackSize >= OCTREE_STACK_SIZE)
            return false;

        stack[stackSize].packed = childPacked;
        stack[stackSize].remainingMask = childPacked & 0xFFu;
        stack[stackSize].boundsMin = bestMin;
        stack[stackSize].size = halfSize;
        stack[stackSize].tEnter = bestEnter;

        ++stackSize;
    }

    return false;
}

[require(spvDescriptorHeapEXT)]
[shader("compute")]
[numthreads(16, 16, 1)]
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

    uint voxelId;
    int3 voxel;
    float tHit;
    uint traversalSteps;

    TraceOctree(
        nodes,
        ray,
        parameters.chunkSize,
        voxelId,
        voxel,
        tHit,
        traversalSteps);

    // Shade misses too: a ray can traverse many nodes without a hit.
    float3 color = TraversalHeatmap(traversalSteps);

    outputTexture[pixel] = float4(color, 1.0f);
}