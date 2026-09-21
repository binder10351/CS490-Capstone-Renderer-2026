# Shaders

Place GLSL shader sources here. Compile them to SPIR-V as part of the build before
the renderer begins creating graphics pipelines.

`gltf_mesh.vert` is the shared mesh vertex shader. `gltf_lit.frag` is the
low-cost multi-light Lambert/Blinn-Phong path; use it while building the first
forward renderer. `gltf_ggx.frag` is the matching direct-light GGX path.

Both fragment shaders require the same descriptor layout:

- set 0, binding 0: `FrameUniforms` (vertex stage)
- set 0, binding 1: `LightingUniforms` (fragment stage), with up to 8 lights
- set 1, binding 0: `MaterialUniforms` (fragment stage)

For the mesh transform, set `viewProjection = projection * inverse(cameraToWorld)`
once per frame, set `cameraWorldPosition` to the translation of `cameraToWorld`,
and update `model` for each glTF node draw. Lights are world-space. Point lights
use `positionOrDirection.w = 1`; directional lights use `w = 0` and a direction
pointing from the light toward the scene.
The shaders do not yet sample glTF textures, normal maps, image-based lighting,
or shadow maps. Add shadow-map descriptors and a per-light shadow index when the
shadow pass is introduced.
