// glTF normals are linear tangent-space data, with +Y along the bitangent.
layout(set = 1, binding = 1) uniform sampler2D normalMap;

vec3 surfaceNormal() {
    vec3 N = normalize(inWorldNormal);
    // Pipeline front-face winding already accounts for mirrored instances.
    const float faceSign = material.alphaParameters.z > 0.5 && !gl_FrontFacing ? -1.0 : 1.0;
    if (material.normalParameters.y < 0.5 || lighting.textureOptions.x < 0.5) return N * faceSign;

    vec3 T;
    float handedness;
    if (abs(inWorldTangent.w) > 0.5) {
        T = inWorldTangent.xyz - N * dot(N, inWorldTangent.xyz);
        handedness = sign(inWorldTangent.w);
    } else {
        // Match the Khronos sample renderer's derivative fallback: derive T
        // from UVs and construct B = cross(N, T). Inferring handedness from
        // dP/dV here reverses the normal-map Y axis on CompareNormal.glb.
        // Explicit tangents above still preserve authored mirrored-UV signs.
        vec3 dx = dFdx(inWorldPosition), dy = dFdy(inWorldPosition);
        vec2 ux = dFdx(inUv), uy = dFdy(inUv);
        float determinant = ux.x * uy.y - ux.y * uy.x;
        if (abs(determinant) < 1e-10) return N * faceSign;
        T = (dx * uy.y - dy * ux.y) / determinant;
        T -= N * dot(N, T);
        handedness = 1.0;
    }
    if (dot(T, T) < 1e-12) return N * faceSign;
    T = normalize(T);
    vec3 B = cross(N, T) * handedness;
    vec3 mapped = texture(normalMap, inUv).xyz * 2.0 - 1.0;
    mapped.xy *= material.normalParameters.x;
    if (dot(mapped, mapped) < 1e-12) return N * faceSign;
    return normalize(mat3(T, B, N) * normalize(mapped)) * faceSign;
}
