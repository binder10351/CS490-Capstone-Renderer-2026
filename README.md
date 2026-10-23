# CS490 Capstone Renderer 2026

A Vulkan glTF 2.0 viewer built with SDL3, Vulkan Memory Allocator (VMA), and
tinygltf.

## Required installed software

Install these tools before building on Windows:

- **Visual Studio 2022 Community** (or later) with the **Desktop development
  with C++** workload. This provides the MSVC C++20 compiler and Windows SDK.
- **CMake 3.24 or later.** The copy installed with Visual Studio is sufficient.
- **[Vulkan SDK](https://vulkan.lunarg.com/).** Install it from LunarG. It
  provides the Vulkan headers, loader library, validation layers, and shader
  tools used by this project.
- **[vcpkg](https://github.com/microsoft/vcpkg).** Clone it to a stable local
  folder and run `bootstrap-vcpkg.bat` once to create `vcpkg.exe`.
- **Git.** CMake uses it to retrieve tinygltf on the first configuration.

You do **not** need to install or copy SDL3, VMA, tinygltf, or Dear ImGui manually. CMake
uses `vcpkg.json` to install SDL3, VMA, and Dear ImGui (SDL3/Vulkan bindings), and retrieves tinygltf during the
first configure step. An internet connection is therefore required initially.

## Configure and build

From the repository root, set `VULKAN_SDK` to the versioned Vulkan SDK install
directory and supply the path to the vcpkg toolchain file:

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.357.0"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg-2026.07.29/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

Use the actual installation paths on your computer. Do not copy dependencies
into this repository.

## Ownership boundaries

- `Application`: SDL window and event loop.
- `GltfLoader`: tinygltf parsing and CPU-side scene data.
- `Renderer`: Vulkan setup, VMA-backed GPU resources, and draw calls.

## Viewing models and camera controls

Run `gltf_viewer` with no arguments for a cube, or pass a `.gltf` / `.glb` path:

```powershell
.\build\Debug\gltf_viewer.exe path\to\model.glb
```

You can also drag a `.gltf` or `.glb` file onto the running viewer to replace the
current model. The camera automatically frames the new model. Invalid or
unsupported files show an error and leave the current model open. For `.gltf`
files, keep any referenced `.bin` files and images in their original relative
locations; `.glb` files typically bundle the model into one file.

- Left drag: orbit around the current target.
- Shift + left drag, or middle drag: pan.
- Mouse wheel: dolly.
- **F**: frame the model's world bounds.
- **G**: toggle the world axes and XZ ground grid.

Axes are X red, Y green, Z blue. Guides are in meters and do not affect framing.
The initial camera frames the imported scene. Resizing updates the projection's
aspect ratio; press F to refit after changing the window shape.

## Viewer menus

The top bar provides live controls:

- **Lighting**: enable the world directional light, adjust its intensity (0–20),
  color and direction, and adjust ambient intensity (0–2).
- **Shading**: toggle normal mapping, base-color textures, metallic/roughness
  textures, ambient occlusion, emissive, vertex colors, diffuse, and specular.
- **View**: toggle grid/axes, frame the model, or reset lighting/shading defaults.

These controls update frame uniforms immediately, without rebuilding GPU geometry
or changing imported materials. Settings survive model replacement during the
session. A zero light direction disables direct illumination until a direction
is set. Ambient is uniform fill lighting; an environment map is not implemented.
The UI captures mouse/keyboard input while interacting with menus, so sliders do
not orbit the camera. Settings reset when the application exits.

The UI requires Dear ImGui 1.92.8 or newer. The manifest records that minimum
and the vcpkg baseline used for the tested dependency set. To update the dependency
set deliberately, update the baseline and rebuild/test the renderer. Keep your
vcpkg checkout recent enough to contain the recorded baseline. `ViewerUi` owns the context and SDL backend; `Renderer` owns the
Vulkan backend and draws the UI after the model.

## Coordinate and transform conventions

The viewer uses right-handed coordinates, +Y up, meters, column-major matrices,
and column vectors. A camera looks along its local -Z axis. Mesh local/model
space is the space of the stored vertices. A node's local transform places it in
its parent's space; its world transform includes all ancestors and the imported
model's root placement:

```text
localToParent = T * R * S
localToWorld  = parentToWorld * localToParent
view         = inverse(cameraToWorld)
clipPosition = projection * view * localToWorld * localPosition
```

`Scene::modelTransform` places the entire model without changing authored node
transforms. `SceneNode::local` supports editable TRS or an intact authored matrix
(including shear); clear `authoredMatrix` before changing TRS. Call
`updateWorldTransforms()` after editing transforms and before querying bounds or
drawing. Nodes sharing a mesh share GPU buffers but have independent transforms.
World guides are independent of model placement.

Vulkan projection uses a [0,1] depth range and one Y correction in the projection
matrix for the positive-height viewport. Lighting, camera position, and surface
normals use world space. Normals use inverse-transpose so nonuniform and mirrored
scales work. Face culling follows each material’s `doubleSided` setting and accounts for
mirrored transforms. Double-sided back faces reverse their shading normals.
Nodes with a singular world transform are omitted because their normal matrix
cannot be inverted.

`Application` owns the CPU scene and orbit controller. `GltfLoader` parses static
triangle geometry and the selected glTF scene hierarchy, including strided and
sparse accessors, node matrices/TRS, shared meshes, core material fields, and flat
per-face normals when absent. Authored tangents are ignored when normals are absent,
as required by glTF. `Renderer` uploads that scene once and accepts its current
transforms and camera each frame. Geometry, texture, sampler, and UV-binding changes require a renderer
rebuild. After editing material factors, alpha settings, or `doubleSided`, call
`Renderer::updateMaterialFactors(scene)` to synchronize the GPU without rebuilding.
This explicit update waits for in-flight work and is intended for occasional edits.
Transform changes do not require a rebuild. Frame UBOs stay persistently mapped and are reused after the corresponding
frame fence, while model and normal matrices are vertex-stage push constants per node. The CPU
computes the inverse-transpose normal matrix and mirrored orientation once per
node per recorded frame, rather than per vertex.

The material path uses direct-light Cook-Torrance GGX shading. Base-color, emissive,
metallic/roughness, ambient-occlusion, and normal textures load from external
images, embedded data URIs, or GLB buffer views. Eight-bit base-color and emissive images use sRGB GPU
formats; normal, metallic/roughness, and AO images remain linear. Sixteen-bit PNG
samples are preserved on the CPU and uploaded as linear half floats; color
channels are decoded from sRGB before upload, while alpha stays linear. Material factors
multiply texture samples. Following the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html),
roughness uses green, metallic uses blue, and AO uses red; AO strength affects
ambient light only. AO may share an ORM image or use a separate image and UV set.
Each texture independently selects its `TEXCOORD_n` attribute and honors glTF
nearest/linear filtering, mip filtering, and repeat/clamp/mirrored wrapping.
Missing textures use neutral fallback images. Decoded images support 8-bit and 16-bit channels.

Normal maps apply the authored scale and UV set. Export tangents for the closest
match to baked maps; meshes without tangents use a derivative-based tangent frame.
Texture-transform and material extensions (including clearcoat) are unsupported.
Optional extensions use the core material fallback and appear in
`Scene::unsupportedExtensions` and logged warnings; unsupported required extensions
produce load errors.

All core material fields are retained, including names, factors, five texture
bindings, alpha mode/cutoff, and `doubleSided`. `COLOR_0` multiplies base color
(including alpha); RGB colors receive alpha 1. OPAQUE ignores authored alpha,
MASK discards values below its cutoff and writes opaque depth, and BLEND uses
straight-alpha blending with depth testing enabled and depth writes disabled. Opaque and
masked primitives render first; blended instances sort back-to-front by their
transformed primitive-bound centers in camera space. Intersecting transparent
geometry can still show sorting artifacts; order-independent transparency is not
implemented.

Image-based lighting, shadow maps, animations, imported cameras/lights, selection
gizmos, and skinning/morphing are not implemented. Non-triangle primitives, skins, and morph
targets produce explicit load errors. Node hierarchy, authored matrix/TRS
transforms, and mesh instancing work for both `.gltf` and `.glb` files.

## CPU scene data

`Scene` owns the imported data after tinygltf is destroyed:

- `roots`, `nodes`, and each node’s `parent`/`children` describe the selected scene.
  `parent` is authoritative; `updateWorldTransforms()` refreshes the child/root lists.
- `meshes` preserve names and primitive grouping. Nodes retain their mesh index
  and a list of shared primitive indices used by the renderer.
- `primitives` own vertices/indices and retain every authored `TEXCOORD_n` and
  `COLOR_n` set. Packed vertices carry the five selected UV streams and `COLOR_0`.
  Flat-normal generation splits triangle corners and remaps all retained streams.
- `materials` own core factors and texture bindings. Slot zero is the implicit
  glTF default material; authored material indices are offset by one.
- Each material `SceneTexture` binding selects a `textures` index and UV set.
  Texture resources reference shared `images` and `samplers`; sampler -1 uses
  default filtering/wrapping. A binding with texture -1 is absent.

Decoded pixels are shared on the CPU, including between materials and slots with
separate samplers. GPU textures currently belong to individual material slots;
this allows one image to be interpreted as both color and linear data. Unused
UV/color sets remain available on the CPU for future rendering features. This is
a static triangle/material representation, rather than a lossless glTF document.

## Tests

With `BUILD_TESTING=ON` (the default), build and run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Tests cover parent/root composition, hierarchy cycles, camera inverse and depth
mapping, shared mesh instances, mirrored transforms, world bounds, sparse
accessors, invalid accessor ranges, and framing at multiple aspect ratios. Material
tests cover glTF/GLB image decoding, PBR factors, sampler settings, independent UV
selection, normal-map handedness, and rejection of missing texture coordinates.

Core material tests cover glTF and GLB defaults, emissive bindings, alpha settings,
shared images/samplers, normalized RGB/RGBA colors, multiple UV/color sets, 16-bit
PNG decoding, flat normals, and invalid material/attribute references. Regenerate
the fixtures with `python tests/assets/generate_core_materials.py`.

Optional Vulkan integration checks require a working Vulkan display:

```sh
cmake -S . -B build -DBUILD_RENDERER_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The renderer test exercises all blend/culling/winding variants, material-factor
updates, loaded GLB resources, and swapchain resizing. It skips if SDL cannot
create a Vulkan window. On X11, with Pillow and ImageMagick installed, compare
actual framebuffer pixels using:

```sh
python3 tests/check_material_frames.py build/renderer_material_tests /tmp/material-frames
```

This checks alpha modes, sorted transparency, emissive color space (8/16-bit),
vertex color, mirrored winding, and double-sided lighting. It also writes PNG
captures. Xvfb plus a software Vulkan driver can supply the display in CI.
