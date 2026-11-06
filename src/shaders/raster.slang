struct Parameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    uint textureIndex;
    uint samplerIndex;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
struct VertexInput { float3 position; float3 normal; float2 uv; uint tile; };
struct VertexOutput {
    float4 position : SV_Position;
    float3 normal;
    float2 uv;
    nointerpolation uint tile;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float3 worldPosition = input.position + parameters.chunkOrigin.xyz;
    output.position = mul(parameters.viewProjection, float4(worldPosition, 1.0f));
    output.normal = input.normal;
    output.uv = input.uv;
    output.tile = input.tile;

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
    float2 pixel = clamp(input.uv * tileSize, 0.5f, tileSize - 0.5f);
    float2 atlasUV = (pixel + float2(float(input.tile) * tileSize, 0.0f)) / float2(width, height);
    return atlas.Sample(pixelSampler, atlasUV);
}
