# Shaders

Place GLSL shader sources here. Compile them to SPIR-V as part of the build before
the renderer begins creating graphics pipelines.

`gltf_mesh.vert` is the shared mesh vertex shader. `gltf_lit.frag` is the
optional low-cost multi-light Lambert/Blinn-Phong path. `gltf_ggx.frag` is the
active direct-light GGX path.

Both fragment shaders require the same descriptor layout:

- set 0, binding 0: `FrameUniforms` (vertex stage)
- set 0, binding 1: `LightingUniforms` (fragment stage), with up to 8 lights
- set 1, binding 0: `MaterialUniforms` (fragment stage)
- set 1, binding 1: linear normal map (`sampler2D`, fragment stage)
- set 1, binding 2: sRGB base-color map
- set 1, binding 3: linear metallic/roughness map (B/G channels)
- set 1, binding 4: linear occlusion map (R channel)

For the mesh transform, set `viewProjection = projection * inverse(cameraToWorld)`
once per frame, set `cameraWorldPosition` to the translation of `cameraToWorld`,
and set the vertex-stage `ObjectUniforms` push constants (112 bytes) for each
glTF node draw: the 64-byte model matrix followed by three padded `vec4` columns
of its inverse-transpose normal matrix. The first normal column stores the
world transform orientation sign in `w` for tangent handedness. The CPU computes
these values once per node per recorded frame, shared by all its primitives. `FrameUniforms` contains only `viewProjection` (64 bytes).
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
Vertex locations 4, 5, and 6 carry independently selected base-color,
metallic/roughness, and AO UVs; location 2 carries normal UVs. Textures preserve
sampler filtering and wrapping. Missing color/data textures bind white so scalar
material factors remain effective. Base-color samples multiply the linear color
factor, and metallic/roughness samples multiply their scalar factors. AO strength
interpolates between unoccluded ambient and the sampled occlusion; direct light
is unaffected. Image-based lighting, shadow maps, and alpha coverage are not yet
implemented.

The GGX path uses a stable distribution peak and height-correlated Smith
visibility, consistent with the
[Khronos Sample Renderer BRDF](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/brdf.glsl).
`tests/Pbr.cpp` compiles the scalar functions in `pbr_brdf.glsl` directly and
checks the analytical peak, normal-incidence visibility, reciprocity, and finite
values at grazing angles. Avoid a fixed epsilon on the squared GGX distribution
denominator: it suppresses smooth-surface highlights.

Lighting currently consists of one directional light and diffuse ambient.
The background clear color is not a reflection environment. Smooth metals need
specular image-based lighting (a roughness-prefiltered environment and BRDF
integration) to show surroundings; correct material channels alone cannot
reproduce an environment-lit sample viewer. HDR exposure/tone mapping is also
not implemented, so bright direct highlights can clip at the swapchain output.
