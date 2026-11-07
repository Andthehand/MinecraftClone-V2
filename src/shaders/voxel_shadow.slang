// Matches the packed CPU header in VoxelShadowVolume.cpp.
struct VoxelShadowHeader {
    float3 minimumVoxel;
    int voxelsPerSide;
    int brickSize;
    uint voxelOccupancyByteOffset;
    uint brickOccupancyByteOffset;
    int bricksPerSide;
};

static const float SHADOW_BLOCKED = 0.0f;
static const float SHADOW_CLEAR = 1.0f;
static const float SHADOW_UNKNOWN = -1.0f;
static const float DIRECTION_EPSILON = 0.0000001f;
static const float UNREACHABLE_DISTANCE = 1e30f;

[require(spvDescriptorHeapEXT)]
VoxelShadowHeader LoadVoxelShadowHeader(ByteAddressBuffer volume) {
    VoxelShadowHeader header;
    // Keep scalar loads: the packed buffer does not use float3 struct alignment.
    header.minimumVoxel = float3(asint(volume.Load(0)), asint(volume.Load(4)), asint(volume.Load(8)));
    header.voxelsPerSide = int(volume.Load(12));
    header.brickSize = int(volume.Load(16));
    header.voxelOccupancyByteOffset = volume.Load(20);
    header.brickOccupancyByteOffset = volume.Load(24);
    header.bricksPerSide = int(volume.Load(28));
    return header;
}

[require(spvDescriptorHeapEXT)]
bool IsOccupied(ByteAddressBuffer volume, uint occupancyByteOffset, uint index) {
    uint wordIndex = index / 32u;
    uint bitIndex = index % 32u;
    uint word = volume.Load(occupancyByteOffset + wordIndex * 4u);
    return (word & (1u << bitIndex)) != 0;
}

uint VoxelShadowGridIndex(int3 position, int cellsPerSide) {
    return uint((position.z * cellsPerSide + position.y) * cellsPerSide + position.x);
}

float MinimumDistance(float3 distances) {
    return min(distances.x, min(distances.y, distances.z));
}

float InverseRayDirection(float direction) {
    return abs(direction) > DIRECTION_EPSILON ? 1.0f / direction : UNREACHABLE_DISTANCE;
}

// Distances are measured from the original ray start, even after skipping a brick.
// An axis parallel to a boundary never crosses it.
float3 BoundaryDistances(float3 boundary, float3 rayStart, float3 rayDirection, float3 inverseDirection) {
    return float3(
        abs(rayDirection.x) > DIRECTION_EPSILON ? (boundary.x - rayStart.x) * inverseDirection.x : UNREACHABLE_DISTANCE,
        abs(rayDirection.y) > DIRECTION_EPSILON ? (boundary.y - rayStart.y) * inverseDirection.y : UNREACHABLE_DISTANCE,
        abs(rayDirection.z) > DIRECTION_EPSILON ? (boundary.z - rayStart.z) * inverseDirection.z : UNREACHABLE_DISTANCE
    );
}

[require(spvDescriptorHeapEXT)]
float TraceVoxelSun(uint volumeIndex, float3 worldPosition, float3 normal, float3 rayDirection, float maxDistance) {
    ByteAddressBuffer volume = ByteAddressBuffer.Handle(uint2(volumeIndex, 0));
    VoxelShadowHeader header = LoadVoxelShadowHeader(volume);

    // Start just outside the surface to avoid hitting its own voxel.
    float3 rayStart = worldPosition + normal * 0.002f + rayDirection * 0.001f - header.minimumVoxel;
    int3 voxelPosition = int3(floor(rayStart));
    int3 voxelStep = int3(
        rayDirection.x > 0 ? 1 : -1,
        rayDirection.y > 0 ? 1 : -1,
        rayDirection.z > 0 ? 1 : -1
    );
    int3 forwardBoundaryOffset = int3(voxelStep.x > 0 ? 1 : 0, voxelStep.y > 0 ? 1 : 0, voxelStep.z > 0 ? 1 : 0);
    float3 inverseDirection = float3(
        InverseRayDirection(rayDirection.x),
        InverseRayDirection(rayDirection.y),
        InverseRayDirection(rayDirection.z)
    );
    float3 distancePerVoxel = abs(inverseDirection);
    float3 nextVoxelBoundaryDistances = BoundaryDistances(
        float3(voxelPosition + forwardBoundaryOffset), rayStart, rayDirection, inverseDirection
    );

    for (uint iteration = 0; iteration < 256; ++iteration) {
        // Unknown coverage is treated as unshadowed sunlight by the caller.
        if (any(voxelPosition < 0) || any(voxelPosition >= header.voxelsPerSide)) {
            return SHADOW_UNKNOWN;
        }

        int3 brickPosition = voxelPosition / header.brickSize;
        uint brickIndex = VoxelShadowGridIndex(brickPosition, header.bricksPerSide);
        if (!IsOccupied(volume, header.brickOccupancyByteOffset, brickIndex)) {
            // The whole brick is air: jump to its next boundary instead of checking its voxels.
            float3 brickExitBoundary = float3((brickPosition + forwardBoundaryOffset) * header.brickSize);
            float3 brickExitDistances = BoundaryDistances(brickExitBoundary, rayStart, rayDirection, inverseDirection);
            float brickExitDistance = MinimumDistance(brickExitDistances);
            if (brickExitDistance >= maxDistance) {
                return SHADOW_CLEAR;
            }

            // Move just past the boundary, then restart voxel stepping in the next brick.
            voxelPosition = int3(floor(rayStart + rayDirection * (brickExitDistance + 0.0001f)));
            nextVoxelBoundaryDistances = BoundaryDistances(
                float3(voxelPosition + forwardBoundaryOffset), rayStart, rayDirection, inverseDirection
            );
            continue;
        }

        // An occupied brick can still contain air; check the individual voxel.
        uint voxelIndex = VoxelShadowGridIndex(voxelPosition, header.voxelsPerSide);
        if (IsOccupied(volume, header.voxelOccupancyByteOffset, voxelIndex)) {
            return SHADOW_BLOCKED;
        }

        float nextVoxelDistance = MinimumDistance(nextVoxelBoundaryDistances);
        if (nextVoxelDistance >= maxDistance) {
            return SHADOW_CLEAR;
        }

        // Advance every tied axis so rays crossing an edge or corner step consistently.
        int3 crossedAxes = int3(
            nextVoxelBoundaryDistances.x <= nextVoxelDistance + 0.000001f ? 1 : 0,
            nextVoxelBoundaryDistances.y <= nextVoxelDistance + 0.000001f ? 1 : 0,
            nextVoxelBoundaryDistances.z <= nextVoxelDistance + 0.000001f ? 1 : 0
        );
        voxelPosition += voxelStep * crossedAxes;
        nextVoxelBoundaryDistances += distancePerVoxel * float3(crossedAxes);
    }

    // The iteration limit was reached before visibility could be established.
    return SHADOW_UNKNOWN;
}
