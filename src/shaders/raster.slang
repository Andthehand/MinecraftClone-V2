struct Parameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
struct VertexInput { float3 position; float3 normal; };
struct VertexOutput { float4 position : SV_Position; float3 normal; };

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float3 worldPosition = input.position + parameters.chunkOrigin.xyz;
    output.position = mul(parameters.viewProjection, float4(worldPosition, 1.0f));
    output.normal = input.normal;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float light = 0.25f + 0.75f * max(dot(normalize(input.normal), normalize(float3(-0.4f, 0.8f, -0.3f))), 0.0f);
    return float4(float3(0.35f, 0.7f, 0.3f) * light, 1.0f);
}
