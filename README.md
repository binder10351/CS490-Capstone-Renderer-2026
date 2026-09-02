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
