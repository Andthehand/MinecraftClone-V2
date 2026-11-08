#pragma once
#include "shadow_map_data.slang"
[require(spvDescriptorHeapEXT)]
float MapVisibility(uint sceneIndex, SceneData data, float3 worldPosition, float3 normal, uint cascade) {
    float4 light = TransformSceneMatrix(sceneIndex, 64u + cascade * 64u,
                                        float4(worldPosition + normal * 0.002f, 1.0f));
    float3 projected = light.xyz / light.w;
    float2 uv = projected.xy * 0.5f + 0.5f;

    // The receiver plane is used to bias the depth comparison to avoid self-shadowing artifacts.
    if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z < 0.0f || projected.z > 1.0f) {
        return 1.0f;
    }

    uint face = abs(normal.x) > 0.5f ? (normal.x > 0.0f ? 0u : 1u)
        : abs(normal.y) > 0.5f ? (normal.y > 0.0f ? 2u : 3u) : (normal.z > 0.0f ? 4u : 5u);
    
    ByteAddressBuffer rawScene = ByteAddressBuffer.Handle(uint2(sceneIndex, 0));
    float2 depthGradient = asfloat(rawScene.Load2(304u + (cascade * 6u + face) * 16u));
    float2 texelCenter = (floor(uv / data.settings.y) + 0.5f) * data.settings.y;

    Texture2DArray<float> shadow = Texture2DArray<float>.Handle(uint2(data.indices.x, 0));
    SamplerComparisonState sampler = SamplerComparisonState.Handle(uint2(data.indices.y, 0));
    
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 sampleUV = texelCenter + float2(x, y) * data.settings.y;
            // Compare against the receiver plane at the actual sampled texel center.
            // A large constant/slope bias instead detaches contact shadows and loses thin casters.
            float referenceDepth = projected.z + dot(depthGradient, sampleUV - uv) - data.settings.x;
            
            visibility += shadow.SampleCmpLevelZero(sampler, float3(sampleUV, float(cascade)), referenceDepth);
        }
    }

    // Average the 3x3 samples to reduce aliasing and provide a smooth transition between cascades.
    return visibility / 9.0f;
}

[require(spvDescriptorHeapEXT)]
float CascadeVisibility(uint sceneIndex, SceneData data, float3 worldPosition, float3 normal, float viewDepth) {
    uint cascade = viewDepth < data.splits.x ? 0u : viewDepth < data.splits.y ? 1u : 2u;
    float visibility = MapVisibility(sceneIndex, data, worldPosition, normal, cascade);
    
    if (cascade < 2u) {
        float end = data.splits[cascade];
        float start = end * (1.0f - data.splits.w);
        if (viewDepth > start) {
            float next = MapVisibility(sceneIndex, data, worldPosition, normal, cascade + 1u);
            visibility = lerp(visibility, next, smoothstep(start, end, viewDepth));
        }
    }

    return visibility;
}

