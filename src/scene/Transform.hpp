#pragma once
#include "math/Math.hpp"
#include <optional>

struct Transform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    // Preserve authored matrices, including shear. Clear before editing TRS.
    std::optional<glm::mat4> authoredMatrix;

    [[nodiscard]] glm::mat4 matrix() const {
        if (authoredMatrix) return *authoredMatrix;
        return glm::translate(glm::mat4(1.0f), translation) *
            glm::mat4_cast(glm::normalize(rotation)) * glm::scale(glm::mat4(1.0f), scale);
    }
};
