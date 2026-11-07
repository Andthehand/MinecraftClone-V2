// Shared by terrain fog and the sky so fully faded geometry matches its backdrop.
float3 SkyColor(float3 direction) {
    const float3 horizon = float3(0.70f, 0.80f, 0.91f);
    const float3 zenith = float3(0.20f, 0.43f, 0.78f);
    float elevation = smoothstep(0.0f, 0.65f, direction.y);
    
    return lerp(horizon, zenith, elevation);
}

float DaylightAmount(float3 sunDirection) {
    return smoothstep(-0.10f, 0.16f, sunDirection.y);
}

float3 SkyColor(float3 direction, float3 sunDirection) {
    float elevation = smoothstep(0.0f, 0.65f, direction.y);
    float3 night = lerp(float3(0.012f, 0.018f, 0.035f), float3(0.003f, 0.006f, 0.020f), elevation);
    float3 sky = lerp(night, SkyColor(direction), DaylightAmount(sunDirection));
    // Share the atmospheric glow with distance fog so the horizon stays continuous.
    float glow = pow(saturate(dot(direction, sunDirection)), 96.0f) * smoothstep(-0.08f, 0.02f, sunDirection.y);
    return sky + float3(1.0f, 0.70f, 0.32f) * glow * 0.10f;
}
