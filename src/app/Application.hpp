#pragma once

#include <SDL3/SDL.h>

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    SDL_Window* window_ = nullptr;
};
