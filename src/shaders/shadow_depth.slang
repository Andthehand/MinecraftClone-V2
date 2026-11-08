struct Parameters {
    column_major float4x4 viewProjection;
    float4 origin;
    uint vertices;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[require(spvDescriptorHeapEXT)]
[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position {
    ByteAddressBuffer vertices = ByteAddressBuffer.Handle(uint2(parameters.vertices, 0));

    // Mul the vertex position by the view-projection matrix to transform it into clip space.
    return mul(parameters.viewProjection, float4(asfloat(vertices.Load3(id * 40u)) + parameters.origin.xyz, 1.0f));
}
