#include "shadow_uniforms.glsl"
layout(set = 0, binding = 3) uniform sampler2DArrayShadow shadowMaps;

int shadowCascade() {
    float depth = -(shadows.view * vec4(inWorldPosition, 1.0)).z;
    if (depth < shadows.splitDepths.w || depth > shadows.splitDepths.z) return -1;
    return depth <= shadows.splitDepths.x ? 0 : (depth <= shadows.splitDepths.y ? 1 : 2);
}

float worldLightVisibility() {
    if (shadows.options.x < 0.5) return 1.0;
    int cascade = shadowCascade();
    if (cascade < 0) return 1.0;
    vec4 projected = shadows.lightViewProjection[cascade] * vec4(inWorldPosition, 1.0);
    vec3 coordinate = projected.xyz / projected.w;
    vec2 uv = coordinate.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ||
        coordinate.z < 0.0 || coordinate.z > 1.0) return 1.0;
    // Bias follows geometric normals, so normal maps do not move shadow edges.
    vec3 normal = normalize(inWorldNormal);
    vec3 lightDirection = normalize(-lighting.lights[0].positionOrDirection.xyz);
    float slope = 1.0 - abs(dot(normal, lightDirection));
    float bias = shadows.options.z * (1.0 + 2.0 * slope);
    // Explicit gradients avoid implicit derivatives across cascade/coverage branches.
    float visibility = textureGrad(shadowMaps, vec4(uv, float(cascade), coordinate.z - bias), vec2(0.0), vec2(0.0));
    return mix(1.0, visibility, shadows.options.y);
}

vec3 showCascade(vec3 color) {
    if (shadows.options.x < 0.5 || shadows.options.w < 0.5) return color;
    int cascade = shadowCascade();
    if (cascade < 0) return color;
    const vec3 colors[3] = vec3[3](vec3(1.0,0.15,0.15), vec3(0.15,1.0,0.15), vec3(0.15,0.3,1.0));
    return mix(color, colors[cascade], 0.35);
}
