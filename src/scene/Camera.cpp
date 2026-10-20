#include "Camera.hpp"
#include <stdexcept>

glm::mat4 Camera::cameraToWorld() const {
    return glm::translate(glm::mat4(1), position) * glm::mat4_cast(glm::normalize(orientation));
}
glm::mat4 Camera::view() const { return glm::inverse(cameraToWorld()); }
glm::mat4 Camera::projection(float aspect) const {
    if (!(aspect > 0 && nearPlane > 0 && farPlane > nearPlane && verticalFov > 0 && verticalFov < glm::pi<float>()))
        throw std::runtime_error("Invalid camera projection");
    auto matrix = glm::perspectiveRH_ZO(verticalFov, aspect, nearPlane, farPlane);
    matrix[1][1] *= -1; // Vulkan positive-height viewport: correct Y exactly once.
    return matrix;
}
