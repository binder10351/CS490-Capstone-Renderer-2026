#version 450
#extension GL_GOOGLE_include_directive : require
#include "material_uniforms.glsl"
#include "shadow_uniforms.glsl"
layout(set = 1, binding = 2) uniform sampler2D baseColorMap;
layout(location = 0) in vec2 inUv;
layout(location = 1) in float inAlpha;
void main() {
    if (material.alphaParameters.x == 1.0) {
        float alpha = material.baseColorFactor.a;
        if (shadows.alphaOptions.x > 0.5) alpha *= texture(baseColorMap, inUv).a;
        if (shadows.alphaOptions.y > 0.5) alpha *= inAlpha;
        if (alpha < material.alphaParameters.y) discard;
    }
}
