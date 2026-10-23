#version 450

// Mesh pass shared by gltf_lit.frag and gltf_ggx.frag.
// The renderer should provide positions, normals, and UVs in these locations.
//
// Space convention:
//   local position --model--> world position --viewProjection--> clip position
// On the CPU, set viewProjection = projection * inverse(cameraToWorld).
// `model` changes per glTF node/draw; `viewProjection` normally changes once
// per frame. World position is retained for fragment lighting calculations.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inBaseColorUv;
layout(location = 5) in vec2 inMetallicRoughnessUv;
layout(location = 6) in vec2 inOcclusionUv;
layout(location = 7) in vec2 inEmissiveUv;
layout(location = 8) in vec4 inColor;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;
layout(location = 3) out vec4 outWorldTangent;
layout(location = 4) out vec2 outBaseColorUv;
layout(location = 5) out vec2 outMetallicRoughnessUv;
layout(location = 6) out vec2 outOcclusionUv;
layout(location = 7) out vec2 outEmissiveUv;
layout(location = 8) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform FrameUniforms {
    // projection * view. The view matrix is the inverse camera world transform.
    mat4 viewProjection;
} frame;

layout(push_constant) uniform ObjectUniforms {
    mat4 model;
    vec4 normalColumn0; // xyz: column 0, w: world transform orientation sign
    vec4 normalColumn1;
    vec4 normalColumn2;
} object;

void main() {
    // w = 1 makes translation in the model matrix affect this position.
    const vec4 worldPosition = object.model * vec4(inPosition, 1.0);
    // glTF node transforms are affine, so worldPosition.w is 1.0 here.
    outWorldPosition = worldPosition.xyz;
    // CPU computes the inverse-transpose once per node, including nonuniform scale.
    const mat3 normalMatrix = mat3(object.normalColumn0.xyz,
                                  object.normalColumn1.xyz, object.normalColumn2.xyz);
    outWorldNormal = normalMatrix * inNormal;
    outUv = inUv;
    outBaseColorUv = inBaseColorUv;
    outMetallicRoughnessUv = inMetallicRoughnessUv;
    outOcclusionUv = inOcclusionUv;
    outEmissiveUv = inEmissiveUv;
    outColor = inColor;
    outWorldTangent = vec4(mat3(object.model) * inTangent.xyz,
                          inTangent.w * object.normalColumn0.w);
    gl_Position = frame.viewProjection * worldPosition;
}
