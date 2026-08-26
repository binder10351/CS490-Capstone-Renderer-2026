# CS490 Capstone Renderer 2026

A Vulkan glTF 2.0 viewer built with SDL3, Vulkan Memory Allocator (VMA), and
tinygltf.

## Prerequisites

- CMake 3.24 or later
- A C++20 compiler (Visual Studio 2022 on Windows)
- [Vulkan SDK](https://vulkan.lunarg.com/)
- [vcpkg](https://github.com/microsoft/vcpkg)

## Configure and build

From the repository root, supply the path to your vcpkg toolchain file:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

`vcpkg.json` is a manifest, so CMake will obtain SDL3, VMA, and tinygltf during
configuration. Do not copy those dependencies into this repository.

## Ownership boundaries

- `Application`: SDL window and event loop.
- `GltfLoader`: tinygltf parsing and CPU-side scene data.
- `Renderer`: Vulkan setup, VMA-backed GPU resources, and draw calls.
