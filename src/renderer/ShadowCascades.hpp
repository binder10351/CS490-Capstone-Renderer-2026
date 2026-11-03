#pragma once
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

inline constexpr std::uint32_t shadowCascadeCount = 3;
inline constexpr std::uint32_t shadowMapResolution = 2048;

struct ShadowCascades {
    std::array<glm::mat4, shadowCascadeCount> lightViewProjection{};
    std::array<float, shadowCascadeCount> splitDepths{};
};

// Receiver coverage follows the camera frustum; the depth range also includes
// scene bounds so casters outside the camera view can still shadow receivers.
inline ShadowCascades buildShadowCascades(const Camera& camera, float aspect,
    glm::vec3 lightDirection, float shadowDistance, const Bounds& sceneBounds,
    std::uint32_t resolution = shadowMapResolution) {
    if (!(aspect > 0 && camera.nearPlane > 0 && camera.farPlane > camera.nearPlane &&
        camera.verticalFov > 0 && camera.verticalFov < glm::pi<float>() &&
        shadowDistance > camera.nearPlane && glm::length(lightDirection) > 1e-6f && resolution > 4))
        throw std::runtime_error("Invalid shadow cascade parameters");
    ShadowCascades result;
    const float nearPlane = camera.nearPlane;
    const float farPlane = std::min(camera.farPlane, shadowDistance);
    const auto pose = camera.cameraToWorld();
    const auto direction = glm::normalize(lightDirection);
    const auto up = std::abs(direction.y) > 0.99f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    // A world-anchored light basis keeps texel snapping stable during translation.
    const auto lightView = glm::lookAtRH(glm::vec3(0), direction, up);
    float sceneMinZ = std::numeric_limits<float>::max(), sceneMaxZ = -sceneMinZ;
    if (sceneBounds.valid()) {
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner{i & 1 ? sceneBounds.max.x : sceneBounds.min.x,
                i & 2 ? sceneBounds.max.y : sceneBounds.min.y,
                i & 4 ? sceneBounds.max.z : sceneBounds.min.z};
            const float z = (lightView * glm::vec4(corner, 1)).z;
            sceneMinZ = std::min(sceneMinZ, z);
            sceneMaxZ = std::max(sceneMaxZ, z);
        }
    }
    float previousSplit = nearPlane;
    for (std::uint32_t cascade = 0; cascade < shadowCascadeCount; ++cascade) {
        const float fraction = static_cast<float>(cascade + 1) / shadowCascadeCount;
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, fraction);
        const float uniform = nearPlane + (farPlane - nearPlane) * fraction;
        const float split = cascade + 1 == shadowCascadeCount ? farPlane : glm::mix(uniform, logarithmic, 0.65f);
        result.splitDepths[cascade] = split;
        const glm::vec3 localCenter(0, 0, -0.5f * (previousSplit + split));
        std::array<glm::vec3, 8> corners;
        float radius = 0;
        for (int i = 0; i < 8; ++i) {
            const float depth = i & 4 ? split : previousSplit;
            const float height = std::tan(camera.verticalFov * 0.5f) * depth;
            const glm::vec3 local{i & 1 ? height * aspect : -height * aspect,
                i & 2 ? height : -height, -depth};
            radius = std::max(radius, glm::length(local - localCenter));
            corners[i] = glm::vec3(pose * glm::vec4(local, 1));
        }
        // Fixed for a given slice/FOV/aspect, independent of camera rotation.
        radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.001f);
        radius *= static_cast<float>(resolution) / (resolution - 2); // Snap margin.
        const float texel = 2.0f * radius / resolution;
        auto center = glm::vec3(lightView * pose * glm::vec4(localCenter, 1));
        center.x = std::round(center.x / texel) * texel;
        center.y = std::round(center.y / texel) * texel;
        float minZ = sceneMinZ, maxZ = sceneMaxZ;
        for (const auto corner : corners) {
            const float z = (lightView * glm::vec4(corner, 1)).z;
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
        const float padding = std::max((maxZ - minZ) * 0.01f, 0.01f);
        auto projection = glm::orthoRH_ZO(center.x - radius, center.x + radius,
            center.y - radius, center.y + radius, -maxZ - padding, -minZ + padding);
        projection[1][1] *= -1;
        projection[3][1] *= -1; // Flip the entire clip-space Y row, including translation.
        result.lightViewProjection[cascade] = projection * lightView;
        previousSplit = split;
    }
    return result;
}
