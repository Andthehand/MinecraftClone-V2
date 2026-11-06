#include "atmosphere.slang"

struct Parameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    float4 cameraPositionFogStart;
    float fogEnd;
    uint textureIndex;
    uint samplerIndex;
    uint vertexBufferIndex;
    uint shadowDataIndex;
    uint shadowTextureIndex;
    uint shadowSamplerIndex;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct ShadowData {
    column_major float4x4 viewProjection;
    float4 sunDirectionRadius;
    float4 texelBiasFade;
};

[require(spvDescriptorHeapEXT)]
float ShadowVisibility(float3 worldPosition, float3 normal, float distance, ShadowData data) {
    float fade = smoothstep(data.texelBiasFade.z, data.sunDirectionRadius.w, distance);
    if (fade >= 1.0f) {
        return 1.0f;
    }

    float4 lightPosition = mul(data.viewProjection, float4(worldPosition + normal * data.texelBiasFade.w, 1.0f));
    float3 projected = lightPosition.xyz / lightPosition.w;
    float2 uv = projected.xy * 0.5f + 0.5f;
    if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z < 0.0f || projected.z > 1.0f) {
        return 1.0f;
    }

    Texture2D<float> shadow = Texture2D<float>.Handle(uint2(parameters.shadowTextureIndex, 0));
    SamplerComparisonState shadowSampler = SamplerComparisonState.Handle(uint2(parameters.shadowSamplerIndex, 0));

    // Filter comparison results for a soft edge without blending depth values.
    float visibility = 0.0f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            visibility += shadow.SampleCmpLevelZero(shadowSampler,
                uv + float2(x, y) * data.texelBiasFade.x, projected.z - data.texelBiasFade.y);
        }
    }
    return lerp(visibility / 9.0f, 1.0f, fade);
}

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv;
    nointerpolation uint tile;
    nointerpolation float3 normal;
    float occlusion;
    float3 worldPosition;
};

[require(spvDescriptorHeapEXT)]
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertexBufferIndex, 0));
    // Match VoxelVertex's packed CPU layout; byte loads avoid float3 alignment differences.
    uint offset = vertexID * 40u;
    float3 position = asfloat(vertices.Load3(offset));
    float3 normal = asfloat(vertices.Load3(offset + 12u));
    float2 uv = asfloat(vertices.Load2(offset + 24u));
    uint tile = vertices.Load(offset + 32u);
    float occlusion = asfloat(vertices.Load(offset + 36u));
    float3 worldPosition = position + parameters.chunkOrigin.xyz;

    VertexOutput output;
    output.position = mul(parameters.viewProjection, float4(worldPosition, 1.0f));
    output.uv = uv;
    output.tile = tile;
    
    // Chunks are translated only, so mesh normals are already in world space.
    output.normal = normal;
    output.occlusion = occlusion;
    output.worldPosition = worldPosition;

    return output;
}

[require(spvDescriptorHeapEXT)]
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    Texture2D atlas = Texture2D.Handle(uint2(parameters.textureIndex, 0));
    SamplerState pixelSampler = SamplerState.Handle(uint2(parameters.samplerIndex, 0));
    uint width, height;
    atlas.GetDimensions(width, height);
    // Clamp within each tile's texel centers so adjacent materials cannot bleed.
    float tileSize = float(height);
    float2 pixel = clamp(frac(input.uv) * tileSize, 0.5f, tileSize - 0.5f);
    float2 atlasUV = (pixel + float2(float(input.tile) * tileSize, 0.0f)) / float2(width, height);
    
    float4 albedo = atlas.Sample(pixelSampler, atlasUV);
    // Direction toward a fixed sun. Ambient light keeps unlit faces readable.
    StructuredBuffer<ShadowData> shadowData = StructuredBuffer<ShadowData>.Handle(uint2(parameters.shadowDataIndex, 0));
    ShadowData sun = shadowData[0];
    float3 sunDirection = sun.sunDirectionRadius.xyz;
    const float ambientLight = 0.45f;
    const float sunIntensity = 0.65f;
    float sunlight = saturate(dot(normalize(input.normal), sunDirection));
    float3 toSurface = input.worldPosition - parameters.cameraPositionFogStart.xyz;
    float distance = length(toSurface);
    float visibility = sunlight > 0.0f ? ShadowVisibility(input.worldPosition, input.normal, distance, sun) : 1.0f;
    float lighting = ambientLight + sunIntensity * sunlight * visibility;
    float3 litColor = albedo.rgb * lighting * input.occlusion;
    float fog = smoothstep(parameters.cameraPositionFogStart.w, parameters.fogEnd, distance);
    float3 fogColor = SkyColor(toSurface / max(distance, 0.0001f));
    
    return float4(lerp(litColor, fogColor, fog), albedo.a);
}
