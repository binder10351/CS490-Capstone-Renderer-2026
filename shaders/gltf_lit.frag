#version 450
#extension GL_GOOGLE_include_directive : require

// Low-cost forward shader: Lambert diffuse plus Blinn-Phong highlights.
// It deliberately shares bindings with gltf_ggx.frag so materials can switch
// between the two shaders without changing descriptor layouts.
// All positions and light colors are in world/linear space. The renderer's
// sRGB swapchain converts the final linear RGB output for display.

const uint MAX_LIGHTS = 8u;

struct Light {
    // xyz: world-space position (point) or direction (directional); w: 1 point, 0 directional
    vec4 positionOrDirection;
    // rgb: linear radiance/color; w: intensity
    vec4 colorIntensity;
};

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inWorldTangent;
layout(location = 4) in vec2 inBaseColorUv;
layout(location = 5) in vec2 inMetallicRoughnessUv;
layout(location = 6) in vec2 inOcclusionUv;
layout(location = 7) in vec2 inEmissiveUv;
layout(location = 8) in vec4 inColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1, std140) uniform LightingUniforms {
    // Camera translation from cameraToWorld; used to form the view direction.
    vec4 cameraWorldPosition;
    vec4 ambientColor;
    Light lights[MAX_LIGHTS];
    // x is the number of active lights. Kept as vec4 for predictable std140 layout.
    vec4 lightCount;
    vec4 textureOptions;
    vec4 shadingOptions;
} lighting;

layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    // glTF baseColorFactor, stored in linear space before upload.
    vec4 baseColorFactor;
    // x metallicFactor, y roughnessFactor, z occlusionStrength, w unused.
    vec4 metallicRoughnessOcclusion;
    vec4 emissiveFactor;
    vec4 normalParameters;
    vec4 alphaParameters;
} material;

#include "normal_mapping.glsl"
#include "material_textures.glsl"

void main() {
    // Positions and normals arrived from gltf_mesh.vert in world space.
    vec4 baseColor = material.baseColorFactor *
        (lighting.textureOptions.y > 0.5 ? texture(baseColorMap, inBaseColorUv) : vec4(1.0)) *
        (lighting.shadingOptions.y > 0.5 ? inColor : vec4(1.0));
    if (material.alphaParameters.x == 1.0 && baseColor.a < material.alphaParameters.y) discard;
    if (material.alphaParameters.x != 2.0) baseColor.a = 1.0;
    const vec2 mr = lighting.textureOptions.z > 0.5 ? texture(metallicRoughnessMap, inMetallicRoughnessUv).bg : vec2(1.0);
    const float metallic = clamp(material.metallicRoughnessOcclusion.x * mr.x, 0.0, 1.0);
    const float ao = mix(1.0, texture(occlusionMap, inOcclusionUv).r,
                         material.metallicRoughnessOcclusion.z * lighting.textureOptions.w);
    const vec3 N = surfaceNormal();
    const vec3 V = normalize(lighting.cameraWorldPosition.xyz - inWorldPosition);
    const float roughness = clamp(material.metallicRoughnessOcclusion.y * mr.y, 0.04, 1.0);
    // Lower roughness produces a tighter highlight. This is an artistic approximation.
    const float shininess = mix(256.0, 4.0, roughness);
    vec3 color = lighting.ambientColor.rgb * baseColor.rgb * (1.0 - metallic) * ao * lighting.shadingOptions.z;

    const uint count = min(uint(lighting.lightCount.x), MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i) {
        const Light light = lighting.lights[i];
        const bool isPoint = light.positionOrDirection.w > 0.5;
        // Directional light xyz points from light toward the scene, so negate
        // it to obtain the surface-to-light direction used by BRDF lighting.
        const vec3 toLight = isPoint
            ? light.positionOrDirection.xyz - inWorldPosition
            : -light.positionOrDirection.xyz;
        const float distanceSquared = max(dot(toLight, toLight), 1e-4);
        const vec3 L = normalize(toLight);
        const vec3 H = normalize(V + L);
        // This is deliberately simple inverse-square attenuation. Add range
        // smoothing later if you import glTF punctual light ranges.
        const float attenuation = isPoint ? 1.0 / distanceSquared : 1.0;
        const float NdotL = max(dot(N, L), 0.0);
        const float specular = pow(max(dot(N, H), 0.0), shininess);
        const vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;

        // A small dielectric F0 keeps non-metal surfaces from looking completely matte.
        const vec3 F0 = mix(vec3(0.04), baseColor.rgb,
                            metallic);
        color += radiance * NdotL * (baseColor.rgb * (1.0 - metallic) * lighting.shadingOptions.z + F0 * specular * lighting.shadingOptions.w);
    }

    // Per-light shadow-map visibility can be applied during light accumulation.
    color += material.emissiveFactor.rgb * texture(emissiveMap, inEmissiveUv).rgb * lighting.shadingOptions.x;
    outColor = vec4(color, baseColor.a);
}
