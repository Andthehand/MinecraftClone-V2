struct ComputeParameters {
	uint width;
	uint height;
	// TODO: Add support for slang preprocessor CHUNK_SIZE
	uint chunkSize;
	float time;

	uint chunkBufferIndex;
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
	float fovY = radians(70.0f);
	float tanHalfFovY = tan(fovY * 0.5f);

	float3 target = float3((float)chunkSize * 0.5f, (float)chunkSize * 0.5f, (float)chunkSize * 0.5f);
	float orbitRadius = (float)chunkSize * 1.2f;
	float angularSpeed = 0.5f;
	float angle = time * angularSpeed;

	float3 cameraPosition = target + float3(cos(angle) * orbitRadius, (float)chunkSize * 0.75f, sin(angle) * orbitRadius);
	float3 forward = normalize(target - cameraPosition);
	float3 worldUp = float3(0.0f, 1.0f, 0.0f);
	float3 right = normalize(cross(worldUp, forward));
	float3 up = normalize(cross(forward, right));

	float3 direction = normalize(forward + right * (ndc.x * aspect * tanHalfFovY) + up * (ndc.y * tanHalfFovY));
	return Ray(cameraPosition, direction);
}

bool IntersectAabb(Ray ray, float3 boundsMin, float3 boundsMax, out float tEnter, out float tExit) {
	float3 invDir = 1.0f / ray.direction;
	float3 t0 = (boundsMin - ray.origin) * invDir;
	float3 t1 = (boundsMax - ray.origin) * invDir;

	float3 tMin3 = min(t0, t1);
	float3 tMax3 = max(t0, t1);

	tEnter = max(max(tMin3.x, tMin3.y), tMin3.z);
	tExit = min(min(tMax3.x, tMax3.y), tMax3.z);
	return tExit >= max(tEnter, 0.0f);
}

uint FlattenVoxelIndex(int3 voxel, uint chunkSize) {
	uint x = (uint)voxel.x;
	uint y = (uint)voxel.y;
	uint z = (uint)voxel.z;
	return x + y * chunkSize + z * chunkSize * chunkSize;
}

float2 ComputeVoxelHitUv(Ray ray, int3 voxel, float tCurrent) {
	float3 hitPosition = ray.origin + ray.direction * tCurrent;
	float3 local = saturate(hitPosition - float3(voxel));

	float dMinX = abs(local.x);
	float dMaxX = abs(1.0f - local.x);
	float dMinY = abs(local.y);
	float dMaxY = abs(1.0f - local.y);
	float dMinZ = abs(local.z);
	float dMaxZ = abs(1.0f - local.z);

	float minDistance = dMinX;
	uint face = 0u;
	if (dMaxX < minDistance) { minDistance = dMaxX; face = 1u; }
	if (dMinY < minDistance) { minDistance = dMinY; face = 2u; }
	if (dMaxY < minDistance) { minDistance = dMaxY; face = 3u; }
	if (dMinZ < minDistance) { minDistance = dMinZ; face = 4u; }
	if (dMaxZ < minDistance) { face = 5u; }

	if (face == 0u) {
		return float2(local.z, local.y);
	}
	if (face == 1u) {
		return float2(1.0f - local.z, local.y);
	}
	if (face == 2u) {
		return float2(local.x, local.z);
	}
	if (face == 3u) {
		return float2(local.x, 1.0f - local.z);
	}
	if (face == 4u) {
		return float2(1.0f - local.x, local.y);
	}

	return float2(local.x, local.y);
}

[require(spvDescriptorHeapEXT)]
[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID) {
	if (threadId.x >= parameters.width || threadId.y >= parameters.height) {
		return;
	}

	uint2 pixel = threadId.xy;
	StructuredBuffer<uint> chunk = StructuredBuffer<uint>.Handle(uint2(parameters.chunkBufferIndex, 0));
	RWTexture2D<float4> outputTexture = RWTexture2D<float4>.Handle(uint2(parameters.outputTextureIndex, 0));

	Ray ray = GeneratePrimaryRay(pixel, parameters.width, parameters.height, parameters.chunkSize, parameters.time);

	float3 color = float3(0.56f, 0.78f, 0.95f);
	float3 boundsMin = float3(0.0f, 0.0f, 0.0f);
	float3 boundsMax = float3((float)parameters.chunkSize, (float)parameters.chunkSize, (float)parameters.chunkSize);

	float tEnter;
	float tExit;
	if (!IntersectAabb(ray, boundsMin, boundsMax, tEnter, tExit)) {
		outputTexture[pixel] = float4(color, 1.0f);
		return;
	}

	float tStart = max(tEnter, 0.0f);
	float tCurrent = tStart;
	float3 startPos = ray.origin + ray.direction * (tStart + 1e-4f);
	int3 voxel = int3(floor(startPos));

	int size = (int)parameters.chunkSize;
	voxel = clamp(voxel, int3(0, 0, 0), int3(size - 1, size - 1, size - 1));

	int3 step = int3(sign(ray.direction));

	float3 tDelta = float3(
		step.x != 0 ? abs(1.0f / ray.direction.x) : 1e30f,
		step.y != 0 ? abs(1.0f / ray.direction.y) : 1e30f,
		step.z != 0 ? abs(1.0f / ray.direction.z) : 1e30f);

	float3 nextBoundary = float3(voxel) + float3(step.x > 0 ? 1 : 0, step.y > 0 ? 1 : 0, step.z > 0 ? 1 : 0);
	float3 tMax = float3(
		step.x != 0 ? (nextBoundary.x - startPos.x) / ray.direction.x : 1e30f,
		step.y != 0 ? (nextBoundary.y - startPos.y) / ray.direction.y : 1e30f,
		step.z != 0 ? (nextBoundary.z - startPos.z) / ray.direction.z : 1e30f);

	bool hit = false;
	float2 hitUv = float2(0.0f, 0.0f);
	uint maxSteps = parameters.chunkSize * 3;
	for (uint i = 0; i < maxSteps; ++i) {
		if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0 || voxel.x >= size || voxel.y >= size || voxel.z >= size) {
			break;
		}

		uint index = FlattenVoxelIndex(voxel, parameters.chunkSize);
		if (chunk[index] != 0u) {
			hit = true;
			hitUv = ComputeVoxelHitUv(ray, voxel, tCurrent);
			break;
		}

		// Check X axis against Z
		if (tMax.x < tMax.y) {
			if (tMax.x < tMax.z) {
				voxel.x += step.x;
				tCurrent = tStart + tMax.x;
				tMax.x += tDelta.x;
			} else {
				voxel.z += step.z;
				tCurrent = tStart + tMax.z;
				tMax.z += tDelta.z;
            }
        // Check Y axis against Z
		} else {
			if (tMax.y < tMax.z) {
				voxel.y += step.y;
				tCurrent = tStart + tMax.y;
				tMax.y += tDelta.y;
			} else {
				voxel.z += step.z;
				tCurrent = tStart + tMax.z;
				tMax.z += tDelta.z;
			}
		}

		if (tCurrent > tExit) {
			break;
		}
	}

	if (hit) {
		color = float3(hitUv, 0.0f);
	}

	outputTexture[pixel] = float4(color, 1.0f);
}
