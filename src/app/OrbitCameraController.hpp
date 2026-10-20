#pragma once
#include "scene/Scene.hpp"
#include "scene/Camera.hpp"
#include <SDL3/SDL.h>

class OrbitCameraController {
public:
    Camera camera;
    void frame(const Bounds& bounds, float aspect);
    void handleEvent(const SDL_Event& event, int drawableHeight);
private:
    void updateCamera();
    glm::vec3 target_{0};
    float distance_ = 3;
    float yaw_ = 0.6f;
    float pitch_ = 0.35f;
    float radius_ = 1;
};
