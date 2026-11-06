struct Parameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    uint textureIndex;
    uint samplerIndex;
    uint vertexBufferIndex;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
struct VertexOutput {
    float4 position : SV_Position;
    float2 uv;
    nointerpolation uint tile;
};

[require(spvDescriptorHeapEXT)]
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID) {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertexBufferIndex, 0));
    // Match VoxelVertex's packed CPU layout; byte loads avoid float3 alignment differences.
    uint offset = vertexID * 36u;
    float3 position = asfloat(vertices.Load3(offset));
    float2 uv = asfloat(vertices.Load2(offset + 24u));
    uint tile = vertices.Load(offset + 32u);
    float3 worldPosition = position + parameters.chunkOrigin.xyz;

    VertexOutput output;
    output.position = mul(parameters.viewProjection, float4(worldPosition, 1.0f));
    output.uv = uv;
    output.tile = tile;

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
    
    return atlas.Sample(pixelSampler, atlasUV);
}
