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

You do **not** need to install or copy SDL3, VMA, or tinygltf manually. CMake
uses `vcpkg.json` to install SDL3 and VMA, and retrieves tinygltf during the
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
scales work. Face culling is disabled; nodes with a singular world transform are
omitted because their normal matrix cannot be inverted.

`Application` owns the CPU scene and orbit controller. `GltfLoader` parses static
triangle geometry and the selected glTF scene hierarchy, including strided and
sparse accessors, node matrices/TRS, shared meshes, material factors, and generated
normals when absent. `Renderer` uploads that scene once and accepts its current
transforms and camera each frame. Geometry/material changes require a renderer
rebuild; transform changes do not. Frame UBOs are updated after the corresponding
frame fence, while model matrices are vertex-stage push constants per draw.

Normal maps are loaded from embedded or external glTF images, uploaded as linear
RGBA8, and sampled by both lighting shaders with the authored normal scale and UV
set. Export tangents for the closest match to baked maps; meshes without tangents
use a derivative-based tangent frame. Normal maps currently use linear filtering
and repeat wrapping; texture transforms are unsupported.

Color/metallic-roughness textures, alpha blending, animations, imported cameras/lights, selection gizmos,
and skinning/morphing are not implemented. Unsupported required extensions,
non-triangle primitives, skins, and morph targets produce explicit load errors.

## Coordinate tests

With `BUILD_TESTING=ON` (the default), build and run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Tests cover parent/root composition, hierarchy cycles, camera inverse and depth
mapping, shared mesh instances, mirrored transforms, world bounds, sparse
accessors, invalid accessor ranges, and framing at multiple aspect ratios.
