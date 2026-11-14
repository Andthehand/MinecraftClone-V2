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
    // Hardware PCF shares one reference depth across its four texels. Cover the
    // receiver-plane variation within that bilinear footprint as well as the base bias.
    // A contributing texel center can be up to one texel away on either axis.
    float depthBias = data.settings.x
        + data.settings.y * (abs(depthGradient.x) + abs(depthGradient.y));

    Texture2DArray<float> shadow = Texture2DArray<float>.Handle(uint2(data.indices.x, 0));
    SamplerComparisonState sampler = SamplerComparisonState.Handle(uint2(data.indices.y, 0));
    
    float visibility = 0.0f;
    // GPU Gems chapter 11: sixteen bilinear comparison taps at offsets
    // -1.5, -0.5, 0.5, 1.5. Keep the footprint continuous instead of snapping to texels.
    [unroll] for (int y = 0; y < 4; ++y) {
        [unroll] for (int x = 0; x < 4; ++x) {
            float2 offsetUV = (float2(x, y) - 1.5f) * data.settings.y;
            float2 sampleUV = uv + offsetUV;
            float referenceDepth = projected.z + dot(depthGradient, offsetUV) - depthBias;
            
            visibility += shadow.SampleCmpLevelZero(sampler, float3(sampleUV, float(cascade)), referenceDepth);
        }
    }

    return visibility / 16.0f;
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

