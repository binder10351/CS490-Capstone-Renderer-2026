cmake_minimum_required(VERSION 3.24)

project(CS490CapstoneRenderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

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
    src/app/ViewerUi.cpp
    src/renderer/Renderer.cpp
    src/scene/GltfLoader.cpp
    src/scene/Scene.cpp
    src/scene/Camera.cpp
    src/app/OrbitCameraController.cpp
)

target_include_directories(gltf_viewer PRIVATE src)

target_link_libraries(gltf_viewer PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    GPUOpen::VulkanMemoryAllocator
    tinygltf
    glm::glm
    imgui::imgui
)

target_compile_features(gltf_viewer PRIVATE cxx_std_20)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADERS
    fullscreen.vert
    fullscreen.frag
    gltf_mesh.vert
    gltf_lit.frag
    gltf_ggx.frag
)
foreach(SHADER IN LISTS SHADERS)
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER}.spv"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_SOURCE_DIR}/shaders/${SHADER}"
                -o "${SHADER_OUTPUT_DIR}/${SHADER}.spv"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${SHADER}"
                "${CMAKE_SOURCE_DIR}/shaders/mesh_fragment.glsl"
                "${CMAKE_SOURCE_DIR}/shaders/pbr_brdf.glsl"
        VERBATIM
    )
    list(APPEND SPIRV_SHADERS "${SHADER_OUTPUT_DIR}/${SHADER}.spv")
endforeach()
add_custom_target(shaders DEPENDS ${SPIRV_SHADERS})
add_dependencies(gltf_viewer shaders)

if(MSVC)
    target_compile_options(gltf_viewer PRIVATE /W4 /permissive-)
else()
    target_compile_options(gltf_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET gltf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_OUTPUT_DIR}
            $<TARGET_FILE_DIR:gltf_viewer>/shaders
)

include(CTest)
if(BUILD_TESTING)
    add_executable(pbr_tests tests/Pbr.cpp)
    add_test(NAME pbr COMMAND pbr_tests)
    add_executable(coordinate_tests
        tests/Coordinates.cpp
        src/scene/Scene.cpp
        src/scene/Camera.cpp
        src/scene/GltfLoader.cpp
        src/app/OrbitCameraController.cpp
    )
    target_include_directories(coordinate_tests PRIVATE src)
    target_link_libraries(coordinate_tests PRIVATE glm::glm tinygltf SDL3::SDL3)
    target_compile_definitions(coordinate_tests PRIVATE TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    add_test(NAME coordinates COMMAND coordinate_tests)
    add_executable(material_tests tests/Materials.cpp src/scene/Scene.cpp src/scene/GltfLoader.cpp)
    target_include_directories(material_tests PRIVATE src)
    target_link_libraries(material_tests PRIVATE glm::glm tinygltf)
    target_compile_definitions(material_tests PRIVATE TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    add_test(NAME materials COMMAND material_tests)
endif()

# Requires a Vulkan-capable display (or Xvfb and a software Vulkan driver).
option(BUILD_RENDERER_TESTS "Build Vulkan material integration tests" OFF)
if(BUILD_TESTING AND BUILD_RENDERER_TESTS)
    add_executable(renderer_material_tests
        tests/RendererMaterials.cpp src/renderer/Renderer.cpp src/app/ViewerUi.cpp
        src/scene/Scene.cpp src/scene/Camera.cpp src/scene/GltfLoader.cpp)
    target_include_directories(renderer_material_tests PRIVATE src)
    target_link_libraries(renderer_material_tests PRIVATE
        Vulkan::Vulkan SDL3::SDL3 GPUOpen::VulkanMemoryAllocator tinygltf glm::glm imgui::imgui)
    target_compile_definitions(renderer_material_tests PRIVATE TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    add_dependencies(renderer_material_tests shaders)
    add_custom_command(TARGET renderer_material_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:renderer_material_tests>/shaders)
    add_test(NAME renderer_materials COMMAND renderer_material_tests)
    set_tests_properties(renderer_materials PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()
