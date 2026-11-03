#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Push constants: mat4 followed by three padded normal-matrix columns.
// normalColumns[0].w stores the world transform's orientation sign.
struct ObjectUniforms {
    std::array<float, 16> model{};
    std::array<std::array<float, 4>, 3> normalColumns{};
};
static_assert(offsetof(ObjectUniforms, normalColumns) == 64);
static_assert(sizeof(ObjectUniforms) == 112);

struct FrameUniforms {
    std::array<float, 16> viewProjection{};
};
struct GpuLight {
    std::array<float, 4> positionOrDirection{};
    std::array<float, 4> colorIntensity{};
};

struct LightingUniforms {
    std::array<float, 4> cameraWorldPosition{};
    std::array<float, 4> ambientColor{};
    std::array<GpuLight, 8> lights{};
    std::array<float, 4> lightCount{};
    std::array<float, 4> textureOptions{}; // normal, base color, metallic/roughness, AO
    std::array<float, 4> shadingOptions{}; // emissive, vertex colors, diffuse, specular
};
struct MaterialUniforms {
    std::array<float, 4> baseColorFactor{};
    std::array<float, 4> metallicRoughnessOcclusion{};
    std::array<float, 4> emissiveFactor{};
    std::array<float, 4> normalParameters{}; // x scale, y map present
    std::array<float, 4> alphaParameters{}; // x mode (0 opaque, 1 mask, 2 blend), y cutoff, z double sided
};
static_assert(sizeof(FrameUniforms) == 64);
static_assert(sizeof(GpuLight) == 32);
static_assert(sizeof(LightingUniforms) == 336);
static_assert(offsetof(MaterialUniforms, alphaParameters) == 64);
static_assert(sizeof(MaterialUniforms) == 80);

// std140: three light matrices, camera view, split depths + camera near, controls.
struct ShadowUniforms {
    std::array<std::array<float, 16>, 3> lightViewProjection{};
    std::array<float, 16> view{};
    std::array<float, 4> splitDepths{};
    std::array<float, 4> options{}; // enabled, strength, receiver bias, cascade debug
    std::array<float, 4> alphaOptions{}; // base-color textures, vertex colors, unused
};
struct ShadowObjectUniforms {
    ObjectUniforms object;
    std::uint32_t cascade = 0;
};
static_assert(sizeof(ShadowUniforms) == 304);
static_assert(offsetof(ShadowUniforms, options) == 272);
static_assert(offsetof(ShadowObjectUniforms, cascade) == 112);
static_assert(sizeof(ShadowObjectUniforms) == 116);
