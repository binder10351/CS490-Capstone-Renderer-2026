layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    vec4 baseColorFactor;
    // Metallic factor, roughness factor, occlusion strength, unused.
    vec4 metallicRoughnessOcclusion;
    vec4 emissiveFactor;
    // Normal scale, normal-map presence, unused, unused.
    vec4 normalParameters;
    // Alpha mode (0 opaque, 1 mask, 2 blend), cutoff,
    // double-sided flag, unused.
    vec4 alphaParameters;
} material;

