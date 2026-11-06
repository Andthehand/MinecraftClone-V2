#include "atmosphere.slang"

struct Parameters {
    // Inverse projection and camera rotation only: sky has no camera translation.
    column_major float4x4 inverseViewProjection;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct VertexOutput {
    float4 position : SV_Position;
    float3 direction;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 clip = uv * 2.0f - 1.0f;
    
    VertexOutput output;
    output.position = float4(clip, 1.0f, 1.0f);
    output.direction = mul(parameters.inverseViewProjection, float4(clip, 1.0f, 1.0f)).xyz;
    
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return float4(SkyColor(normalize(input.direction)), 1.0f);
}
