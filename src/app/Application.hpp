#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <filesystem>
#include "OrbitCameraController.hpp"
#include "renderer/RenderSettings.hpp"

class Renderer;
class ViewerUi;

class Application {
public:
    explicit Application(const std::filesystem::path& modelPath = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void loadModel(const std::filesystem::path& modelPath);

    Scene scene_;
    OrbitCameraController controller_;
    RenderSettings settings_;
    std::unique_ptr<ViewerUi> ui_;
    bool showGuides_ = true;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
};
