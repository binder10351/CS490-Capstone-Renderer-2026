#include "renderer/ShadowCascades.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b, float tolerance = 1e-4f) { return std::abs(a-b) < tolerance; }
}
int main() {
    try {
        Camera camera;
        camera.nearPlane = 0.1f;
        camera.farPlane = 75;
        Bounds bounds;
        bounds.include({-20,-10,-30}); bounds.include({20,10,15});
        for (const auto direction : {glm::vec3(-0.4f,-1,-0.5f), glm::vec3(0,-1,0), glm::vec3(0,1,0)}) {
            for (const float aspect : {0.4f, 1.0f, 2.0f}) {
                for (const float angle : {0.0f, 0.7f, 2.1f}) {
                    camera.orientation = glm::angleAxis(angle, glm::normalize(glm::vec3(1,2,0.5f)));
                    const auto cascades = buildShadowCascades(camera, aspect, direction, 100, bounds);
                    require(close(cascades.splitDepths.back(), camera.farPlane), "Shadow distance must respect camera far plane");
                    float previous = camera.nearPlane;
                    for (std::size_t cascade = 0; cascade < shadowCascadeCount; ++cascade) {
                        const float split = cascades.splitDepths[cascade];
                        require(split > previous, "Cascade splits must be ordered");
                        const auto matrix = cascades.lightViewProjection[cascade];
                        for (int i = 0; i < 8; ++i) {
                            const float depth = i & 4 ? split : previous;
                            const float height = std::tan(camera.verticalFov * 0.5f) * depth;
                            const glm::vec4 local{i & 1 ? height * aspect : -height * aspect,
                                i & 2 ? height : -height, -depth, 1};
                            const auto clip = matrix * camera.cameraToWorld() * local;
                            require(std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.z), "Finite light-space coordinates");
                            require(std::abs(clip.x) <= 1.0001f && std::abs(clip.y) <= 1.0001f && clip.z >= 0 && clip.z <= 1,
                                "Every receiver frustum corner must fit its cascade with Vulkan depth in [0,1]");
                        }
                        // World origin falls on a texel grid in both shadow axes.
                        for (int axis = 0; axis < 2; ++axis) {
                            const float texels = matrix[3][axis] * shadowMapResolution / 2;
                            require(close(texels, std::round(texels), 0.002f), "Cascade projection must snap to shadow texels");
                        }
                        previous = split;
                    }
                }
            }
        }
        camera.orientation = {1,0,0,0};
        const glm::vec3 direction = glm::normalize(glm::vec3(0.8f,0,-1));
        const glm::vec3 receiver(0,0,0), caster = receiver - direction * 200.0f;
        Bounds casterBounds; casterBounds.include(receiver); casterBounds.include(caster);
        const auto cascades = buildShadowCascades(camera, 1, direction, 12, casterBounds);
        require(close(cascades.splitDepths.back(), 12), "Configurable shadow distance");
        const float depth = -(camera.view() * glm::vec4(receiver,1)).z;
        const int cascade = depth <= cascades.splitDepths[0] ? 0 : (depth <= cascades.splitDepths[1] ? 1 : 2);
        const auto receiverClip = cascades.lightViewProjection[cascade] * glm::vec4(receiver,1);
        const auto casterClip = cascades.lightViewProjection[cascade] * glm::vec4(caster,1);
        require(close(receiverClip.x, casterClip.x) && close(receiverClip.y, casterClip.y) &&
            casterClip.z >= 0 && receiverClip.z <= 1 && casterClip.z < receiverClip.z,
            "Off-camera upstream casters must fit depth range and shadow the same receiver texel");
        Camera rotated = camera;
        rotated.orientation = glm::angleAxis(0.8f, glm::vec3(0,1,0));
        const auto rotation = buildShadowCascades(rotated, 1, direction, 12, casterBounds);
        for (std::size_t i = 0; i < shadowCascadeCount; ++i)
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 2; ++row)
                    require(close(cascades.lightViewProjection[i][column][row], rotation.lightViewProjection[i][column][row]),
                        "Camera rotation must not resize cascade footprints");
        camera.position.x += 1e-6f;
        const auto translation = buildShadowCascades(camera, 1, direction, 12, casterBounds);
        for (std::size_t i = 0; i < shadowCascadeCount; ++i)
            for (int row = 0; row < 2; ++row)
                require(close(cascades.lightViewProjection[i][3][row], translation.lightViewProjection[i][3][row]),
                    "Sub-texel movement should preserve shadow projection");
        std::cout << "Cascade coverage, depth, offscreen casters and stabilization checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
