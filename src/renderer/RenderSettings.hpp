#pragma once
#include "math/Math.hpp"

// Viewer controls, independent of imported material data and renderer lifetime.
struct RenderSettings {
    bool worldLightEnabled = true;
    float worldLightIntensity = 2.0f;
    glm::vec3 worldLightColor{1.0f};
    glm::vec3 worldLightDirection{-0.4f, -1.0f, -0.5f};
    float ambientIntensity = 0.15f;
    bool normalMapping = true;
    bool baseColorTextures = true;
    bool metallicRoughnessTextures = true;
    bool occlusion = true;
    bool emissive = true;
    bool vertexColors = true;
    bool diffuse = true;
    bool specular = true;
};
