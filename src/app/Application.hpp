#pragma once

#include <SDL3/SDL.h>

#include <memory>

class Renderer;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
};
