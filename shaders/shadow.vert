#version 450
#extension GL_GOOGLE_include_directive : require
#include "shadow_uniforms.glsl"
layout(location = 0) in vec3 inPosition;
layout(location = 4) in vec2 inBaseColorUv;
layout(location = 8) in vec4 inColor;
layout(location = 0) out vec2 outUv;
layout(location = 1) out float outAlpha;
layout(push_constant) uniform ShadowObject {
    mat4 model;
    vec4 normalColumns[3]; // Match ObjectUniforms padding; unused here.
    uint cascade;
} object;
void main() {
    outUv = inBaseColorUv;
    outAlpha = inColor.a;
    gl_Position = shadows.lightViewProjection[object.cascade] * object.model * vec4(inPosition, 1.0);
}
