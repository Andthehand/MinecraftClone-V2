#pragma once
// Matches the explicitly checked CPU SceneData layout.
struct SceneData {
    column_major float4x4 cameraView;
    column_major float4x4 lightMatrices[3];
    float4 splits;
    float4 settings;
    uint4 indices;
    float4 receiverPlanes[18];
};

// Explicit column loads avoid matrix-array layout/conversion differences in storage buffers.
[require(spvDescriptorHeapEXT)]
float4 TransformSceneMatrix(uint sceneIndex, uint byteOffset, float4 point) {
    ByteAddressBuffer buffer = ByteAddressBuffer.Handle(uint2(sceneIndex, 0));
    
    return asfloat(buffer.Load4(byteOffset)) * point.x
        + asfloat(buffer.Load4(byteOffset + 16u)) * point.y
        + asfloat(buffer.Load4(byteOffset + 32u)) * point.z
        + asfloat(buffer.Load4(byteOffset + 48u)) * point.w;
}
