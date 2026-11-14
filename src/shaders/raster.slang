#include "atmosphere.slang"
#include "shadow_maps.slang"

static const float SHADOW_CLEAR = 1.0f;

// Matches VoxelParameters in rendering/ShaderData.h; field order is part of the upload layout.
struct VoxelParameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    float4 cameraPositionFogStart;
    float fogEnd;
    uint textureIndex;
    uint samplerIndex;
    uint vertexBufferIndex;
    uint sunDataIndex;
};
[[vk::push_constant]] ConstantBuffer<VoxelParameters> parameters;

// Matches the CPU SunData upload: direction at byte 0, shadow-data index at byte 16.
struct SunData {
    float4 sunDirection;
    uint shadowDataIndex;
};

struct VoxelVertex {
    float3 position;
    float3 normal;
    float2 uv;
    uint tile;
    float occlusion;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv;
    nointerpolation uint tile;
    nointerpolation float3 normal;
    float occlusion;
    float3 worldPosition;
};

[require(spvDescriptorHeapEXT)]
VoxelVertex LoadVoxelVertex(uint vertexID) {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertexBufferIndex, 0));
    // Match the packed 40-byte CPU vertex; explicit loads avoid float3 alignment differences.
    uint vertexByteOffset = vertexID * 40u;
    VoxelVertex vertex;
    vertex.position = asfloat(vertices.Load3(vertexByteOffset));
    vertex.normal = asfloat(vertices.Load3(vertexByteOffset + 12u));
    vertex.uv = asfloat(vertices.Load2(vertexByteOffset + 24u));
    vertex.tile = vertices.Load(vertexByteOffset + 32u);
    vertex.occlusion = asfloat(vertices.Load(vertexByteOffset + 36u));

    return vertex;
}

[require(spvDescriptorHeapEXT)]
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    VoxelVertex vertex = LoadVoxelVertex(vertexID);
    float3 worldPosition = vertex.position + parameters.chunkOrigin.xyz;

    VertexOutput output;
    output.position = mul(parameters.viewProjection, float4(worldPosition, 1.0f));
    output.uv = vertex.uv;
    output.tile = vertex.tile;
    // Chunks are translated only, so mesh normals are already in world space.
    output.normal = vertex.normal;
    output.occlusion = vertex.occlusion;
    output.worldPosition = worldPosition;

    return output;
}

[require(spvDescriptorHeapEXT)]
float4 SampleVoxelAlbedo(float2 uv, uint tile) {
    Texture2D atlas = Texture2D.Handle(uint2(parameters.textureIndex, 0));
    SamplerState pixelSampler = SamplerState.Handle(uint2(parameters.samplerIndex, 0));
    uint atlasWidth, atlasHeight;
    atlas.GetDimensions(atlasWidth, atlasHeight);

    // Square tiles form one horizontal strip. Stay within texel centers to prevent bleeding.
    float tileSize = float(atlasHeight);
    float2 tilePixel = clamp(frac(uv) * tileSize, 0.5f, tileSize - 0.5f);
    float2 tileOrigin = float2(float(tile) * tileSize, 0.0f);
    float2 atlasUV = (tileOrigin + tilePixel) / float2(atlasWidth, atlasHeight);

    return atlas.Sample(pixelSampler, atlasUV);
}

[require(spvDescriptorHeapEXT)]
float CalculateSunVisibility(SunData sun, float3 worldPosition, float3 normal, float cameraDistance) {
    StructuredBuffer<SceneData> buffer = StructuredBuffer<SceneData>.Handle(uint2(sun.shadowDataIndex, 0));
    SceneData data = buffer[0];

    if (cameraDistance >= data.settings.w) {
        return SHADOW_CLEAR;
    }
    
    float viewDepth = -TransformSceneMatrix(sun.shadowDataIndex, 0u, float4(worldPosition, 1.0f)).z;
    float visibility = CascadeVisibility(sun.shadowDataIndex, data, worldPosition, normal, viewDepth);
    
    return lerp(visibility, SHADOW_CLEAR, smoothstep(data.settings.z, data.settings.w, cameraDistance));
}

[require(spvDescriptorHeapEXT)]
float3 CalculateLighting(SunData sun, float3 worldPosition, float3 normal, float cameraDistance) {
    float3 sunDirection = sun.sunDirection.xyz;
    // Cool ambient skylight remains in shadow; direct sunlight is gently warm.
    const float3 nightAmbient = float3(0.025f, 0.035f, 0.060f);
    const float3 dayAmbient = float3(0.30f, 0.34f, 0.40f);
    const float3 sunColor = float3(1.00f, 0.94f, 0.82f);
    const float sunIntensity = 0.80f;
    float3 ambientLight = lerp(nightAmbient, dayAmbient, DaylightAmount(sunDirection));

    float surfaceSunlight = saturate(dot(normalize(normal), sunDirection));
    float horizonFade = smoothstep(0.0f, 0.12f, sunDirection.y);
    float sunlight = surfaceSunlight * horizonFade;
    float visibility = SHADOW_CLEAR;

    if (sunlight > 0.0f) {
        visibility = CalculateSunVisibility(sun, worldPosition, normal, cameraDistance);
    }

    return ambientLight + sunColor * sunIntensity * sunlight * visibility;
}

[require(spvDescriptorHeapEXT)]
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    StructuredBuffer<SunData> sunBuffer = StructuredBuffer<SunData>.Handle(uint2(parameters.sunDataIndex, 0));
    SunData sun = sunBuffer[0];
    float3 cameraToSurface = input.worldPosition - parameters.cameraPositionFogStart.xyz;
    float cameraDistance = length(cameraToSurface);

    float4 albedo = SampleVoxelAlbedo(input.uv, input.tile);
    float3 lighting = CalculateLighting(sun, input.worldPosition, input.normal, cameraDistance);
    // Strengthen contact shading in linear light; open faces stay at full brightness.
    const float aoContrast = 5.0f;
    float ambientOcclusion = pow(saturate(input.occlusion), aoContrast);
    float3 litColor = albedo.rgb * lighting * ambientOcclusion;

    float fogStartDistance = parameters.cameraPositionFogStart.w;
    float fogAmount = smoothstep(fogStartDistance, parameters.fogEnd, cameraDistance);
    float3 viewDirection = cameraToSurface / max(cameraDistance, 0.0001f);
    float3 fogColor = SkyColor(viewDirection, sun.sunDirection.xyz);

    return float4(lerp(litColor, fogColor, fogAmount), albedo.a);
}
