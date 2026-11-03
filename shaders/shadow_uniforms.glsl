layout(set = 0, binding = 2, std140) uniform ShadowUniforms {
    mat4 lightViewProjection[3];
    mat4 view;
    vec4 splitDepths; // xyz cascade ends, w camera near
    vec4 options; // enabled, strength, receiver bias, cascade debug
    vec4 alphaOptions; // base-color textures, vertex colors, unused
} shadows;
