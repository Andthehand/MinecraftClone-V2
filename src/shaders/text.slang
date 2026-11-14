struct Parameters {
    float2 inverseSize;
    uint vertices;
    uint textureIndex;
    uint samplerIndex;
    uint linearColor;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv;
    float4 color;
};

[require(spvDescriptorHeapEXT)]
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertices, 0));
    uint offset = vertexID * 32;
    float2 position = asfloat(vertices.Load2(offset));

    VertexOutput output;
    output.position = float4(position * parameters.inverseSize * 2 - 1, 0, 1);
    output.uv = asfloat(vertices.Load2(offset + 8));
    output.color = asfloat(vertices.Load4(offset + 16));

    return output;
}

[require(spvDescriptorHeapEXT)]
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    Texture2D atlas = Texture2D.Handle(uint2(parameters.textureIndex, 0));
    SamplerState fontSampler = SamplerState.Handle(uint2(parameters.samplerIndex, 0));

    float4 color = input.color;
    if (parameters.linearColor != 0) {
        color.rgb = select(color.rgb <= 0.04045,
            color.rgb / 12.92, pow((color.rgb + 0.055) / 1.055, 2.4));
    }

    return float4(color.rgb, color.a * atlas.Sample(fontSampler, input.uv).a);
}
