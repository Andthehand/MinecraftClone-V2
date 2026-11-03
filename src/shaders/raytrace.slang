struct ComputeParameters {
    uint width;
    uint height;
    uint chunkSize; // Octree<depth>::SideLength; root spans [0, chunkSize)^3.
    float time;
    uint chunkBufferIndex; // Packed output of Octree::toGpuNodes().
    uint outputTextureIndex;
};
[[vk::push_constant]] ConstantBuffer<ComputeParameters> parameters;

struct Ray {
    float3 origin;
    float3 direction;
};

Ray GeneratePrimaryRay(uint2 pixel, uint width, uint height, uint chunkSize, float time) {
    float2 resolution = float2((float)width, (float)height);
    float2 uv = (float2(pixel) + 0.5f) / resolution;
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float aspect = resolution.x / resolution.y;
    float tanHalfFovY = tan(radians(70.0f) * 0.5f);
    float3 target = float3((float)chunkSize * 0.5f);
    float orbitRadius = (float)chunkSize * 0.8f;
    float angle = time * 0.5f;
    float3 cameraPosition = target + float3(
        cos(angle) * orbitRadius, (float)chunkSize * 0.75f,
        sin(angle) * orbitRadius);

    float3 forward = normalize(target - cameraPosition);
    float3 right = normalize(cross(float3(0.0f, 1.0f, 0.0f), forward));
    float3 up = normalize(cross(forward, right));
    float3 direction = normalize(forward
        + right * (ndc.x * aspect * tanHalfFovY)
        + up * (ndc.y * tanHalfFovY));

    return Ray(cameraPosition, direction);
}

// Avoid 0 * infinity for rays parallel to a slab.
// Parallel rays use half-open bounds to select exactly one side of a split.
bool IntersectAabb(Ray ray, float3 boundsMin, float3 boundsMax,
                   out float tEnter, out float tExit) {
    tEnter = 0.0f;
    tExit = 3.402823466e+38f;
    for (uint axis = 0u; axis < 3u; ++axis) {
        float origin = ray.origin[axis];
        float direction = ray.direction[axis];
        if (direction == 0.0f) {
            if (origin < boundsMin[axis] || origin >= boundsMax[axis])
                return false;
        } else {
            float a = (boundsMin[axis] - origin) / direction;
            float b = (boundsMax[axis] - origin) / direction;
            tEnter = max(tEnter, min(a, b));
            tExit = min(tExit, max(a, b));
            // Ignore cubes touched only at an edge/corner or behind the ray.
            if (tExit <= tEnter)
                return false;
        }
    }
    return tExit > tEnter;
}

float2 ComputeVoxelHitUv(Ray ray, int3 voxel, float tCurrent) {
    float3 hitPosition = ray.origin + ray.direction * tCurrent;
    float3 local = saturate(hitPosition - float3(voxel));
    float distances[6] = {
        abs(local.x), abs(1.0f - local.x),
        abs(local.y), abs(1.0f - local.y),
        abs(local.z), abs(1.0f - local.z)
    };
    uint face = 0u;
    for (uint i = 1u; i < 6u; ++i)
        if (distances[i] < distances[face])
            face = i;

    switch (face) {
        case 0u: return float2(local.z, local.y);
        case 1u: return float2(1.0f - local.z, local.y);
        case 2u: return float2(local.x, local.z);
        case 3u: return float2(local.x, 1.0f - local.z);
        case 4u: return float2(1.0f - local.x, local.y);
        default: return float2(local.x, local.y);
    }
}

struct TraversalFrame {
    uint packed;
    uint remainingMask;
    float3 boundsMin;
    float size;
    float tEnter;
};

// Root + 31 descendant levels, matching Octree<depth> with depth <= 31.
static const uint OCTREE_STACK_SIZE = 32u;

bool TraceOctree(StructuredBuffer<uint> nodes, Ray ray, uint chunkSize,
                 out uint voxelId, out int3 voxel, out float tHit) {
    voxelId = 0u;
    voxel = int3(0, 0, 0);
    tHit = 0.0f;
    if (chunkSize == 0u)
        return false;

    float rootEnter;
    float rootExit;
    if (!IntersectAabb(ray, float3(0.0f), float3((float)chunkSize),
                       rootEnter, rootExit))
        return false;

    TraversalFrame stack[OCTREE_STACK_SIZE];
    uint stackSize = 1u;
    uint root = nodes[0];
    stack[0].packed = root;
    stack[0].remainingMask = root & 0xFFu;
    stack[0].boundsMin = float3(0.0f);
    stack[0].size = (float)chunkSize;
    stack[0].tEnter = rootEnter;

    while (stackSize != 0u) {
        uint top = stackSize - 1u;
        TraversalFrame frame = stack[top];
        uint childMask = frame.packed & 0xFFu;
        uint payload = frame.packed >> 8u;

        if (childMask == 0u) {
            --stackSize;
            if (payload == 0u)
                continue;

            voxelId = payload;
            tHit = frame.tEnter;
            // For this CPU tree, occupied leaves are one voxel wide.
            // Clamping also keeps UVs valid at the leaf's maximum boundary.
            float3 local = clamp(
                floor(ray.origin + ray.direction * tHit - frame.boundsMin),
                float3(0.0f), float3(max(frame.size - 1.0f, 0.0f)));
            voxel = int3(frame.boundsMin + local);
            return true;
        }

        if (frame.remainingMask == 0u) {
            --stackSize;
            continue;
        }

        // Select the closest intersected child still awaiting traversal.
        // Save the parent on the stack so its other children can be visited
        // if this child's entire subtree is empty along the ray.
        float halfSize = frame.size * 0.5f;
        uint bestChild = 8u;
        float bestEnter = 3.402823466e+38f;
        float3 bestMin = float3(0.0f);
        uint intersectedMask = 0u;

        for (uint child = 0u; child < 8u; ++child) {
            uint bit = 1u << child;
            if ((frame.remainingMask & bit) == 0u)
                continue;

            // Same numbering as the CPU: x + 2*y + 4*z.
            float3 childMin = frame.boundsMin + halfSize * float3(
                (float)(child & 1u), (float)((child >> 1u) & 1u),
                (float)((child >> 2u) & 1u));
            float childEnter;
            float childExit;
            if (!IntersectAabb(ray, childMin, childMin + float3(halfSize),
                               childEnter, childExit))
                continue;

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

        stack[top].remainingMask = intersectedMask & ~(1u << bestChild);
        // Rank uses the ORIGINAL mask, not the remaining traversal mask.
        uint precedingMask = childMask & ((1u << bestChild) - 1u);
        uint childIndex = payload + countbits(precedingMask);
        uint childPacked = nodes[childIndex];

        // Unreachable for valid output of Octree<depth>, depth <= 31.
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
    if (threadId.x >= parameters.width || threadId.y >= parameters.height)
        return;

    StructuredBuffer<uint> nodes = StructuredBuffer<uint>.Handle(uint2(parameters.chunkBufferIndex, 0));
    RWTexture2D<float4> outputTexture = RWTexture2D<float4>.Handle(uint2(parameters.outputTextureIndex, 0));

    uint2 pixel = threadId.xy;
    Ray ray = GeneratePrimaryRay(pixel, parameters.width, parameters.height,
                                 parameters.chunkSize, parameters.time);

    float3 color = float3(0.56f, 0.78f, 0.95f);
    uint voxelId;
    int3 voxel;
    float tHit;
    if (TraceOctree(nodes, ray, parameters.chunkSize, voxelId, voxel, tHit)) {
        color = float3(ComputeVoxelHitUv(ray, voxel, tHit), 0.0f);
    }

    outputTexture[pixel] = float4(color, 1.0f);
}
