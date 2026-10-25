#version 450
#extension GL_GOOGLE_include_directive : require

// Low-cost forward shader: Lambert diffuse plus Blinn-Phong highlights.
// It deliberately shares bindings with gltf_ggx.frag so materials can switch
// between the two shaders without changing descriptor layouts.
// All positions and light colors are in world/linear space. The renderer's
// sRGB swapchain converts the final linear RGB output for display.

#include "mesh_fragment.glsl"

vec3 evaluateBlinnPhong(
    SurfaceMaterial surface,
    vec3 normal,
    vec3 viewDirection,
    LightSample light
) {
    const vec3 reflectance = mix(
        vec3(0.04), surface.baseColor.rgb, surface.metallic);
    // Artistic approximation: lower roughness gives a tighter highlight.
    const float shininess = mix(256.0, 4.0, surface.roughness);

    const vec3 halfDirection = normalize(viewDirection + light.direction);
    const float NdotL = max(dot(normal, light.direction), 0.0);
    const float specular = pow(max(dot(normal, halfDirection), 0.0), shininess);

    const vec3 diffuse = surface.baseColor.rgb * (1.0 - surface.metallic);

    return light.radiance * NdotL * (diffuse * lighting.shadingOptions.z
        + reflectance * specular * lighting.shadingOptions.w);
}

void main() {
    const SurfaceMaterial surface = sampleMaterial();
    const vec3 normal = surfaceNormal();
    const vec3 toCamera = lighting.cameraWorldPosition.xyz - inWorldPosition;
    const vec3 viewDirection = normalize(toCamera);

    vec3 color = ambientLighting(surface);
    const uint count = min(uint(lighting.lightCount.x), MAX_LIGHTS);

    for (uint i = 0u; i < count; ++i) {
        const LightSample light = sampleLight(
            lighting.lights[i], inWorldPosition);
        color += evaluateBlinnPhong(surface, normal, viewDirection, light);
    }

    color += surface.emissive;
    outColor = vec4(color, surface.baseColor.a);
}
