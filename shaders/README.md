# Shaders

Place GLSL shader sources here. Compile them to SPIR-V as part of the build before
the renderer begins creating graphics pipelines.

`gltf_mesh.vert` is the shared mesh vertex shader. `gltf_lit.frag` is the
optional low-cost multi-light Lambert/Blinn-Phong path. `gltf_ggx.frag` is the
active direct-light GGX path.

The fragment entry points follow the same flow: sample the material, establish
world-space shading vectors, accumulate ambient and direct light, then add emissive.
Shared code lives in two includes:

- `mesh_fragment.glsl`: GPU layouts, texture sampling, alpha coverage, normal
  mapping, and light sampling, grouped into functions in one file
- `pbr_brdf.glsl`: scalar GGX equations, kept separate because CPU tests compile
  these same functions directly

Each fragment entry point contains its own lighting-model function and a short
`main()` that assembles the final color. Bindings and uniform member order remain
shared between both lighting models.

Both fragment shaders require the same descriptor layout:

- set 0, binding 0: `FrameUniforms` (vertex stage)
- set 0, binding 1: `LightingUniforms` (fragment stage), with up to 8 lights
- set 1, binding 0: `MaterialUniforms` (fragment stage)
- set 1, binding 1: linear normal map (`sampler2D`, fragment stage)
- set 1, binding 2: sRGB base-color map
- set 1, binding 3: linear metallic/roughness map (B/G channels)
- set 1, binding 4: linear occlusion map (R channel)
- set 1, binding 5: emissive map (sRGB for 8-bit source images)

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
Vertex locations 4, 5, 6, and 7 carry independently selected base-color,
metallic/roughness, AO, and emissive UVs; location 2 carries normal UVs. Location 8
carries linear RGBA vertex color. Textures preserve
sampler filtering and wrapping. Missing color/data textures bind white so scalar
material factors remain effective. Base-color samples multiply the linear color
factor, and metallic/roughness samples multiply their scalar factors. AO strength
interpolates between unoccluded ambient and the sampled occlusion; direct light
is unaffected. Image-based lighting and shadow maps are not yet implemented.

`MaterialUniforms` is 80 bytes: five `vec4`s containing base color,
metallic/roughness/AO strength, emissive, normal scale/presence, and alpha
mode/cutoff/double-sided settings. Both fragment paths multiply base color by
`COLOR_0`, sample emissive RGB, discard MASK coverage, and force OPAQUE/MASK output
alpha to one. The renderer selects depth-writing opaque or non-depth-writing
straight-alpha blend pipelines and sorts transparent instances by camera depth.
Culling and front-face winding are chosen per material and mirrored transform.
Double-sided back faces negate the final world-space shading normal.

Sixteen-bit source images are uploaded as linear RGBA half floats. Color samples
are decoded from sRGB on the CPU before upload; data channels and alpha are linear.

The GGX path uses a stable distribution peak and height-correlated Smith
visibility, consistent with the
[Khronos Sample Renderer BRDF](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/brdf.glsl).
`tests/Pbr.cpp` compiles the scalar functions in `pbr_brdf.glsl` directly and
checks the analytical peak, normal-incidence visibility, reciprocity, and finite
values at grazing angles. Avoid a fixed epsilon on the squared GGX distribution
denominator: it suppresses smooth-surface highlights.

`LightingUniforms` is 336 bytes. The final two `vec4`s carry viewer toggles:
`textureOptions` selects normal/base-color/metallic-roughness/AO texture features;
`shadingOptions` selects emissive/vertex-color/diffuse/specular features. Both
fragment paths honor these flags. Disabling a material texture uses a neutral
sample and retains its authored factor. Disabling emissive removes the entire
emissive contribution. Alpha coverage and material culling remain active.

Lighting currently consists of one directional light and diffuse ambient, with
intensity, direction, color, and shading features controlled from the top menu.
The background clear color is not a reflection environment. Smooth metals need
specular image-based lighting (a roughness-prefiltered environment and BRDF
integration) to show surroundings; correct material channels alone cannot
reproduce an environment-lit sample viewer.

## Fullscreen post processing

Scene shaders output linear HDR radiance to an `R16G16B16A16_SFLOAT` target
(16-bit floating point per channel), with a separate target for each swapchain
image. `fullscreen.vert` draws a fullscreen triangle and `fullscreen.frag`
samples that target after the scene pass. It applies exposure in EV stops,
an ACES fitted tone curve, and optional FXAA using tone-mapped luma for edge
detection. FXAA smooths image edges but does not provide temporal stability or
recover subpixel geometry like temporal AA or supersampling can.

The **Post process** menu controls exposure, tone mapping, and FXAA. Tone mapping
and FXAA default to enabled; disabling tone mapping clamps exposed radiance to
the display range. The sRGB swapchain performs output encoding automatically;
the shader encodes explicitly for a UNORM fallback. ImGui renders after the
fullscreen pass. Add further image effects in `fullscreen.frag`; effects needing
history or multiple passes will require additional targets and synchronization.
