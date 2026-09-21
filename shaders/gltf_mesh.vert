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

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;

layout(set = 0, binding = 0, std140) uniform FrameUniforms {
    // projection * view. The view matrix is the inverse camera world transform.
    mat4 viewProjection;
    // Local-to-world transform for the mesh instance currently being drawn.
    mat4 model;
} frame;

void main() {
    // w = 1 makes translation in the model matrix affect this position.
    const vec4 worldPosition = frame.model * vec4(inPosition, 1.0);
    // glTF node transforms are affine, so worldPosition.w is 1.0 here.
    outWorldPosition = worldPosition.xyz;
    // Normals are directions (not positions), and require inverse-transpose
    // when the model matrix has non-uniform scale.
    outWorldNormal = mat3(transpose(inverse(frame.model))) * inNormal;
    outUv = inUv;
    gl_Position = frame.viewProjection * worldPosition;
}
