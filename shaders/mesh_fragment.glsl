// Shared fragment inputs and GPU layouts. Keep bindings and member order in
// sync
// with the renderer. Positions/directions are world-space; colors are linear.
const uint MAX_LIGHTS = 8u;

struct Light {
    // xyz: point position or light-to-scene direction.
    // w: 1 for point lights, 0 for directional lights.
    vec4 positionOrDirection;
    // rgb: linear color; w: intensity.
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
    // Camera translation from cameraToWorld, not a camera-space position.
    vec4 cameraWorldPosition;
    vec4 ambientColor;
    Light lights[MAX_LIGHTS];
    vec4 lightCount;
    // normal, base color, metallic/roughness, occlusion.
    vec4 textureOptions;
    // emissive, vertex color, diffuse, specular.
    vec4 shadingOptions;
} lighting;

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

// Material textures: color maps are decoded to linear RGB by the texture
// format.
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D baseColorMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D occlusionMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

struct SurfaceMaterial {
    vec4 baseColor;
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;
};

vec4 sampleBaseColor() {
    const vec4 colorSample = lighting.textureOptions.y > 0.5
        ? texture(baseColorMap, inBaseColorUv) : vec4(1.0);
    const vec4 vertexColor = lighting.shadingOptions.y > 0.5
        ? inColor : vec4(1.0);

    return material.baseColorFactor * colorSample * vertexColor;
}

vec4 applyAlphaMode(vec4 baseColor) {
    const float alphaMode = material.alphaParameters.x;
    const float alphaCutoff = material.alphaParameters.y;

    if (alphaMode == 1.0 && baseColor.a < alphaCutoff) {
        discard;
    }

    if (alphaMode != 2.0) {
        baseColor.a = 1.0;
    }

    return baseColor;
}

SurfaceMaterial sampleMaterial() {
    SurfaceMaterial surface;
    surface.baseColor = applyAlphaMode(sampleBaseColor());

    // glTF stores metallic in B and roughness in G.
    const vec2 metallicRoughness = lighting.textureOptions.z > 0.5
        ? texture(metallicRoughnessMap, inMetallicRoughnessUv).bg
        : vec2(1.0);
    const vec2 factors = material.metallicRoughnessOcclusion.xy;

    surface.metallic = clamp(factors.x * metallicRoughness.x, 0.0, 1.0);
    surface.roughness = clamp(factors.y * metallicRoughness.y, 0.04, 1.0);

    const float occlusion = texture(occlusionMap, inOcclusionUv).r;
    const float strength = material.metallicRoughnessOcclusion.z;
    surface.occlusion = mix(
        1.0, occlusion, strength * lighting.textureOptions.w);

    const vec3 emissive = texture(emissiveMap, inEmissiveUv).rgb;
    surface.emissive = material.emissiveFactor.rgb * emissive
        * lighting.shadingOptions.x;

    return surface;
}

// Tangent-space normal mapping, including meshes without authored tangents.
vec3 surfaceNormal() {
    const vec3 normal = normalize(inWorldNormal);
    // Pipeline front-face winding already accounts for mirrored instances.
    const bool backFace = material.alphaParameters.z > 0.5 && !gl_FrontFacing;
    const float faceSign = backFace ? -1.0 : 1.0;

    if (material.normalParameters.y < 0.5 || lighting.textureOptions.x < 0.5) {
        return normal * faceSign;
    }

    vec3 tangent;
    float handedness;

    if (abs(inWorldTangent.w) > 0.5) {
        tangent = inWorldTangent.xyz - normal * dot(normal, inWorldTangent.xyz);
        handedness = sign(inWorldTangent.w);
    } else {
        // Match the Khronos sample renderer's derivative fallback: derive
        // tangent
        // from UVs and construct bitangent = cross(normal, tangent). Using
        // dP/dV for handedness reverses the map Y axis on CompareNormal.glb.
        // Explicit tangents above still preserve authored mirrored-UV signs.
        const vec3 positionDx = dFdx(inWorldPosition);
        const vec3 positionDy = dFdy(inWorldPosition);
        const vec2 uvDx = dFdx(inUv);
        const vec2 uvDy = dFdy(inUv);
        const float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;

        if (abs(determinant) < 1e-10) {
            return normal * faceSign;
        }

        tangent = (positionDx * uvDy.y - positionDy * uvDx.y) / determinant;
        tangent -= normal * dot(normal, tangent);
        handedness = 1.0;
    }

    if (dot(tangent, tangent) < 1e-12) {
        return normal * faceSign;
    }

    tangent = normalize(tangent);
    vec3 bitangent = cross(normal, tangent) * handedness;

    vec3 tangentNormal = texture(normalMap, inUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalParameters.x;

    if (dot(tangentNormal, tangentNormal) < 1e-12) {
        return normal * faceSign;
    }
    const mat3 tangentToWorld = mat3(tangent, bitangent, normal);
    const vec3 worldNormal = tangentToWorld * normalize(tangentNormal);

    return normalize(worldNormal) * faceSign;
}

// Surface-to-light direction and incoming linear radiance for either light
// type.
struct LightSample {
    vec3 direction;
    vec3 radiance;
};

LightSample sampleLight(Light light, vec3 worldPosition) {
    const bool isPoint = light.positionOrDirection.w > 0.5;
    const vec3 toLight = isPoint
        ? light.positionOrDirection.xyz - worldPosition
        : -light.positionOrDirection.xyz;
    const float distanceSquared = max(dot(toLight, toLight), 1e-4);
    const float attenuation = isPoint ? 1.0 / distanceSquared : 1.0;

    LightSample result;
    result.direction = normalize(toLight);
    result.radiance = light.colorIntensity.rgb
        * light.colorIntensity.w * attenuation;
    return result;
}

vec3 ambientLighting(SurfaceMaterial surface) {
    // Occlusion affects ambient diffuse only.
    return lighting.ambientColor.rgb * surface.baseColor.rgb
        * (1.0 - surface.metallic) * surface.occlusion
        * lighting.shadingOptions.z;
}
