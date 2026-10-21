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
- set 1, binding 1: linear normal map (`sampler2D`, fragment stage)

For the mesh transform, set `viewProjection = projection * inverse(cameraToWorld)`
once per frame, set `cameraWorldPosition` to the translation of `cameraToWorld`,
and set the vertex-stage `ObjectUniforms.model` push constant (64 bytes) for each
glTF node draw. `FrameUniforms` contains only `viewProjection` (64 bytes).
Lights are world-space. Point lights
use `positionOrDirection.w = 1`; directional lights use `w = 0` and a direction
pointing from the light toward the scene.
Both shaders sample glTF tangent-space normal maps, applying `normalTexture.scale`
and transforming the result into world space. Vertex location 3 supplies the
authored tangent and handedness; when absent, a derivative-based UV frame is used.
This fallback approximates the tangent basis and may differ from a MikkTSpace bake;
export tangents with the model for a matching basis. Degenerate UVs retain the mesh normal.
The fallback follows the Khronos sample renderer's `B = cross(N, T)` convention;
authored tangents supply their own handedness, including mirrored UV islands.
Materials without maps bind a flat fallback texture and skip normal sampling.
Normal maps currently use linear filtering and repeat wrapping.
The shaders do not yet sample color/metallic-roughness textures, image-based lighting,
or shadow maps. Add shadow-map descriptors and a per-light shadow index when the
shadow pass is introduced.
