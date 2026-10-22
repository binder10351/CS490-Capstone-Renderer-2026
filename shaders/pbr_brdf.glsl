// Scalar BRDF math is also compiled by tests/Pbr.cpp to exercise the shader's
// actual equations. Inputs: unit-vector cosines in [0,1], roughness in [0.04,1].
float distributionGgx(float NdotH, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float cosSquared = NdotH * NdotH;
    // This form preserves alphaSquared at NdotH == 1. A fixed epsilon on
    // PI * denominator^2 destroys the narrow peak for low roughness.
    const float denominator = (1.0 - cosSquared) + cosSquared * alphaSquared;
    const float ratio = alpha / denominator;
    return ratio * ratio / PI;
}

// Height-correlated Smith GGX visibility: includes G / (4 * NdotV * NdotL).
// Matches the visibility model used by the Khronos glTF Sample Renderer.
float visibilityGgx(float NdotV, float NdotL, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float viewTerm = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSquared) + alphaSquared);
    const float lightTerm = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSquared) + alphaSquared);
    const float denominator = viewTerm + lightTerm;
    if (denominator <= 0.0) return 0.0;
    return 0.5 / denominator;
}
