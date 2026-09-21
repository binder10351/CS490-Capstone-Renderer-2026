#version 450

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
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1, std140) uniform LightingUniforms {
    // Camera translation from cameraToWorld; used to form the view direction.
    vec4 cameraWorldPosition;
    vec4 ambientColor;
    Light lights[MAX_LIGHTS];
    // x is the number of active lights. Kept as vec4 for predictable std140 layout.
    vec4 lightCount;
} lighting;

layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    // glTF baseColorFactor, stored in linear space before upload.
    vec4 baseColorFactor;
    // x metallicFactor, y roughnessFactor, z occlusionStrength, w unused.
    vec4 metallicRoughnessOcclusion;
    vec4 emissiveFactor;
} material;

void main() {
    // Positions and normals arrived from gltf_mesh.vert in world space.
    const vec3 N = normalize(inWorldNormal);
    const vec3 V = normalize(lighting.cameraWorldPosition.xyz - inWorldPosition);
    const float roughness = clamp(material.metallicRoughnessOcclusion.y, 0.04, 1.0);
    // Lower roughness produces a tighter highlight. This is an artistic approximation.
    const float shininess = mix(256.0, 4.0, roughness);
    vec3 color = lighting.ambientColor.rgb * material.baseColorFactor.rgb;

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
        const vec3 F0 = mix(vec3(0.04), material.baseColorFactor.rgb,
                            material.metallicRoughnessOcclusion.x);
        color += radiance * NdotL * (material.baseColorFactor.rgb + F0 * specular);
    }

    // Future additions: base-color/normal/metallic-roughness textures and
    // per-light shadow-map visibility belong here, before light accumulation.
    color += material.emissiveFactor.rgb;
    outColor = vec4(color, material.baseColorFactor.a);
}
