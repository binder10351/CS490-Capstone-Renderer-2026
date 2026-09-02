#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 top = vec3(0.08, 0.20, 0.45);
    const vec3 bottom = vec3(0.70, 0.12, 0.32);
    const float glow = 0.08 * sin(inUv.x * 12.0) * sin(inUv.y * 12.0);
    outColor = vec4(mix(bottom, top, inUv.y) + glow, 1.0);
}
