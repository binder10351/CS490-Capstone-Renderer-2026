#pragma once
#include "math/Math.hpp"

// Viewer controls, independent of imported material data and renderer lifetime.
struct RenderSettings {
    bool worldLightEnabled = true;
    float worldLightIntensity = 2.0f;
    glm::vec3 worldLightColor{1.0f};
    glm::vec3 worldLightDirection{-0.4f, -1.0f, -0.5f};
    bool shadows = true;
    float shadowDistance = 100.0f;
    float shadowStrength = 1.0f;
    float shadowBias = 0.0002f; // Normalized shadow depth; scene-scale dependent.
    float shadowSlopeBias = 1.75f;
    bool showShadowCascades = false;
    float ambientIntensity = 0.15f;
    float exposure = 0.0f; // EV compensation, applied before tone mapping.
    bool autoExposure = true;
    float exposureAdaptationSeconds = 1.0f; // Time to close about 63% of the EV gap.
    bool toneMapping = true;
    bool fxaa = true;
    bool normalMapping = true;
    bool baseColorTextures = true;
    bool metallicRoughnessTextures = true;
    bool occlusion = true;
    bool emissive = true;
    bool vertexColors = true;
    bool diffuse = true;
    bool specular = true;
};
