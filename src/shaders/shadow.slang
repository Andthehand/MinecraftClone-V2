struct Parameters {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    uint vertexBufferIndex;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

[require(spvDescriptorHeapEXT)]
[shader("vertex")]
float4 vertexMain(uint vertexID : SV_VertexID) : SV_Position {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertexBufferIndex, 0));
    float3 position = asfloat(vertices.Load3(vertexID * 40u)) + parameters.chunkOrigin.xyz;
    
    return mul(parameters.viewProjection, float4(position, 1.0f));
}
