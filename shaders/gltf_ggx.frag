#version 450
#extension GL_GOOGLE_include_directive : require

// Direct-light glTF metallic-roughness approximation using Cook-Torrance GGX.
// This uses the exact same descriptors as gltf_lit.frag.
// Image-based lighting is not yet implemented.
// All calculations use world-space vectors and linear colors. The camera's
// world-space position is supplied separately from the viewProjection matrix.

#include "mesh_fragment.glsl"

const float PI = 3.14159265359;

#include "pbr_brdf.glsl"

vec3 fresnelSchlick(float HdotV, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
}

vec3 evaluateGgx(
    SurfaceMaterial surface,
    vec3 normal,
    vec3 viewDirection,
    LightSample light
) {
    const vec3 reflectance = mix(
        vec3(0.04), surface.baseColor.rgb, surface.metallic);
    const float NdotL = clamp(dot(normal, light.direction), 0.0, 1.0);
    const float NdotV = clamp(dot(normal, viewDirection), 0.0, 1.0);
    // Opposing view/light directions would produce an undefined half vector.
    if (NdotL <= 0.0 || NdotV <= 0.0) {
        return vec3(0.0);
    }

    const vec3 halfDirection = normalize(viewDirection + light.direction);
    const float NdotH = clamp(dot(normal, halfDirection), 0.0, 1.0);
    const float HdotV = clamp(dot(halfDirection, viewDirection), 0.0, 1.0);

    const vec3 fresnel = fresnelSchlick(HdotV, reflectance);
    const float distribution = distributionGgx(NdotH, surface.roughness);
    const float visibility = visibilityGgx(NdotV, NdotL, surface.roughness);

    const vec3 specular = distribution * visibility * fresnel;
    const vec3 diffuse = (1.0 - fresnel) * (1.0 - surface.metallic) *
                         surface.baseColor.rgb / PI;

    return (diffuse * lighting.shadingOptions.z
        + specular * lighting.shadingOptions.w) * light.radiance * NdotL;
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
        color += evaluateGgx(surface, normal, viewDirection, light);
    }

    color += surface.emissive;
    outColor = vec4(color, surface.baseColor.a);
}
