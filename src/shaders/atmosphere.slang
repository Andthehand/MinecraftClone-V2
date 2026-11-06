// Shared by terrain fog and the sky so fully faded geometry matches its backdrop.
float3 SkyColor(float3 direction) {
    const float3 horizon = float3(0.70f, 0.80f, 0.91f);
    const float3 zenith = float3(0.20f, 0.43f, 0.78f);
    float elevation = smoothstep(0.0f, 0.65f, direction.y);
    
    return lerp(horizon, zenith, elevation);
}
