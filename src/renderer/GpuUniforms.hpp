#pragma once

#include <array>

struct FrameUniforms {
    std::array<float, 16> viewProjection{};
    std::array<float, 16> model{};
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
};
struct MaterialUniforms {
    std::array<float, 4> baseColorFactor{};
    std::array<float, 4> metallicRoughnessOcclusion{};
    std::array<float, 4> emissiveFactor{};
};
static_assert(sizeof(FrameUniforms) == 128);
static_assert(sizeof(GpuLight) == 32);
static_assert(sizeof(LightingUniforms) == 304);
static_assert(sizeof(MaterialUniforms) == 48);