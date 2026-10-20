#include "OrbitCameraController.hpp"
#include <algorithm>
#include <cmath>

void OrbitCameraController::updateCamera() {
    pitch_ = std::clamp(pitch_, -1.55f, 1.55f);
    distance_ = std::clamp(distance_, radius_ * 0.01f, radius_ * 1000.0f);
    const glm::vec3 direction{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
    camera.position = target_ + direction * distance_;
    camera.orientation = glm::quat_cast(glm::inverse(glm::lookAtRH(camera.position, target_, glm::vec3(0,1,0))));
    camera.nearPlane = std::max(radius_ * 0.0001f, distance_ - radius_ * 1.5f);
    camera.farPlane = std::max(distance_ + radius_ * 100.0f, camera.nearPlane * 2.0f);
}
void OrbitCameraController::frame(const Bounds& bounds, float aspect) {
    if (bounds.valid()) {
        target_ = (bounds.min + bounds.max) * 0.5f;
        radius_ = std::max(glm::length(bounds.max - bounds.min) * 0.5f, 0.001f);
    } else { target_ = glm::vec3(0); radius_ = 1; }
    const float halfAngle = std::atan(std::tan(camera.verticalFov * 0.5f) * std::min(std::max(aspect, 0.001f), 1.0f));
    distance_ = radius_ / std::sin(halfAngle) * 1.1f;
    updateCamera();
}
void OrbitCameraController::handleEvent(const SDL_Event& event, int drawableHeight) {
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        const float amount = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        distance_ *= std::exp(-amount * 0.12f);
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if ((event.motion.state & SDL_BUTTON_MMASK) ||
            ((event.motion.state & SDL_BUTTON_LMASK) && (SDL_GetModState() & SDL_KMOD_SHIFT))) {
            int windowHeight = drawableHeight;
            if (auto* window = SDL_GetWindowFromID(event.motion.windowID)) SDL_GetWindowSize(window, nullptr, &windowHeight);
            const float units = 2 * distance_ * std::tan(camera.verticalFov * 0.5f) / std::max(windowHeight, 1);
            target_ += camera.orientation * glm::vec3(-event.motion.xrel * units, event.motion.yrel * units, 0);
        } else if (event.motion.state & SDL_BUTTON_LMASK) {
            yaw_ -= event.motion.xrel * 0.006f;
            pitch_ += event.motion.yrel * 0.006f;
        }
    }
    updateCamera();
}
