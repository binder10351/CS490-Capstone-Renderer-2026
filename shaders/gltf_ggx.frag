#version 450
#extension GL_GOOGLE_include_directive : require

// Direct-light glTF metallic-roughness approximation using Cook-Torrance GGX.
// This uses the exact same descriptors as gltf_lit.frag.
// Image-based lighting is not yet implemented.
// All calculations use world-space vectors and linear colors. The camera's
// world-space position is supplied separately from the viewProjection matrix.

const uint MAX_LIGHTS = 8u;
const float PI = 3.14159265359;

struct Light {
    vec4 positionOrDirection;
    vec4 colorIntensity;
};

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inWorldTangent;
layout(location = 4) in vec2 inBaseColorUv;
layout(location = 5) in vec2 inMetallicRoughnessUv;
layout(location = 6) in vec2 inOcclusionUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1, std140) uniform LightingUniforms {
    // Camera translation from cameraToWorld, not a camera-space position.
    vec4 cameraWorldPosition;
    vec4 ambientColor;
    Light lights[MAX_LIGHTS];
    vec4 lightCount;
} lighting;

layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    vec4 baseColorFactor;
    vec4 metallicRoughnessOcclusion;
    vec4 emissiveFactor;
    vec4 normalParameters;
} material;

#include "normal_mapping.glsl"
#include "material_textures.glsl"

#include "pbr_brdf.glsl"

vec3 fresnelSchlick(float HdotV, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
}

void main() {
    // These varyings are world-space outputs of gltf_mesh.vert.
    const vec4 baseColor = material.baseColorFactor * texture(baseColorMap, inBaseColorUv);
    const vec2 mr = texture(metallicRoughnessMap, inMetallicRoughnessUv).bg;
    const float metallic = clamp(material.metallicRoughnessOcclusion.x * mr.x, 0.0, 1.0);
    const float ao = mix(1.0, texture(occlusionMap, inOcclusionUv).r,
                         material.metallicRoughnessOcclusion.z);
    const vec3 N = surfaceNormal();
    const vec3 V = normalize(lighting.cameraWorldPosition.xyz - inWorldPosition);
    const float roughness = clamp(material.metallicRoughnessOcclusion.y * mr.y, 0.04, 1.0);
    const vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 color = lighting.ambientColor.rgb * baseColor.rgb * (1.0 - metallic) * ao;

    const uint count = min(uint(lighting.lightCount.x), MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i) {
        const Light light = lighting.lights[i];
        const bool isPoint = light.positionOrDirection.w > 0.5;
        // For a directional light, xyz points from the light toward the scene.
        // Negate it to make L point from the shaded point toward the light.
        const vec3 toLight = isPoint
            ? light.positionOrDirection.xyz - inWorldPosition
            : -light.positionOrDirection.xyz;
        const float distanceSquared = max(dot(toLight, toLight), 1e-4);
        const vec3 L = normalize(toLight);
        const float NdotL = clamp(dot(N, L), 0.0, 1.0);
        const float NdotV = clamp(dot(N, V), 0.0, 1.0);
        // Also avoids an undefined half vector when V and L oppose each other.
        if (NdotL <= 0.0 || NdotV <= 0.0) continue;
        const vec3 H = normalize(V + L);
        const float NdotH = clamp(dot(N, H), 0.0, 1.0);
        const float HdotV = clamp(dot(H, V), 0.0, 1.0);
        const float attenuation = isPoint ? 1.0 / distanceSquared : 1.0;

        const vec3 F = fresnelSchlick(HdotV, F0);
        const float D = distributionGgx(NdotH, roughness);
        const float visibility = visibilityGgx(NdotV, NdotL, roughness);
        const vec3 specular = D * visibility * F;
        const vec3 diffuse = (1.0 - F) * (1.0 - metallic) *
                             baseColor.rgb / PI;
        const vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
        // Multiply by a shadow visibility value here once shadow maps exist.
        color += (diffuse + specular) * radiance * NdotL;
    }

    color += material.emissiveFactor.rgb;
    outColor = vec4(color, baseColor.a);
}
