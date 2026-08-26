cmake_minimum_required(VERSION 3.24)

project(CS490CapstoneRenderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

include(FetchContent)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tinygltf)

add_executable(gltf_viewer
    src/main.cpp
    src/app/Application.cpp
    src/renderer/Renderer.cpp
    src/scene/GltfLoader.cpp
)

target_include_directories(gltf_viewer PRIVATE src)

target_link_libraries(gltf_viewer PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    GPUOpen::VulkanMemoryAllocator
    tinygltf
)

target_compile_features(gltf_viewer PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(gltf_viewer PRIVATE /W4 /permissive-)
else()
    target_compile_options(gltf_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET gltf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:gltf_viewer>/shaders
)
