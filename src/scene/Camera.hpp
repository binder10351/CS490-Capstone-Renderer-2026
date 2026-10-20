#pragma once
#include "math/Math.hpp"

struct Camera {
    glm::vec3 position{0, 0, 3};
    glm::quat orientation{1, 0, 0, 0};
    float verticalFov = glm::radians(45.0f);
    float nearPlane = 0.01f;
    float farPlane = 1000.0f;

    [[nodiscard]] glm::mat4 cameraToWorld() const;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;
};
