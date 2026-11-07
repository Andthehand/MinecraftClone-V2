#include "atmosphere.slang"

struct Parameters {
    // Inverse projection and camera rotation only: sky has no camera translation.
    column_major float4x4 inverseViewProjection;
    float4 sunDirectionSize;
    float4 sunRight;
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
    float3 direction = normalize(input.direction);
    float3 sunDirection = parameters.sunDirectionSize.xyz;
    float3 sky = SkyColor(direction, sunDirection);

    // Project the view ray onto a square centered on the sun at infinity.
    // The fixed orbit axis avoids orientation flips as the sun passes overhead.
    float3 right = parameters.sunRight.xyz;
    float3 sunUp = cross(sunDirection, right);
    float forward = dot(direction, sunDirection);
    float2 plane = float2(dot(direction, right), dot(direction, sunUp)) / max(forward, 0.0001f);
    float edge = max(abs(plane.x), abs(plane.y));
    float halfSize = parameters.sunDirectionSize.w;
    float antialias = max(fwidth(edge) * 0.5f, 0.00001f);
    float sun = (1.0f - smoothstep(halfSize - antialias, halfSize + antialias, edge)) * step(0.0f, forward);
    // Clip the moving disc against the horizon instead of drawing it below ground.
    sun *= smoothstep(-0.002f, 0.002f, direction.y);
    // The sky is drawn before terrain, so world geometry naturally hides the sun.
    return float4(lerp(sky, float3(1.0f, 0.96f, 0.82f), sun), 1.0f);
}
