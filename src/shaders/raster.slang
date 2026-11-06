struct Parameters {
    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 chunkOrigin;
    float tanHalfFovY;
    float aspect;
    float nearPlane;
    float farPlane;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
struct VertexInput { float3 position; float3 normal; };
struct VertexOutput { float4 position : SV_Position; float3 normal; };

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    float3 relative = input.position + parameters.chunkOrigin.xyz - parameters.cameraPosition.xyz;
    float depth = dot(relative, parameters.cameraForward.xyz);
    float nearPlane = parameters.nearPlane;
    float farPlane = parameters.farPlane;
    VertexOutput output;

    // Vulkan clip depth is [0,w]; negate Y for a positive-height viewport.
    output.position = float4(
        dot(relative, parameters.cameraRight.xyz) / (parameters.tanHalfFovY * parameters.aspect),
        -dot(relative, parameters.cameraUp.xyz) / parameters.tanHalfFovY,
        farPlane * (depth - nearPlane) / (farPlane - nearPlane), depth);
    output.normal = input.normal;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float light = 0.25f + 0.75f * max(dot(normalize(input.normal), normalize(float3(-0.4f, 0.8f, -0.3f))), 0.0f);
    return float4(float3(0.35f, 0.7f, 0.3f) * light, 1.0f);
}
