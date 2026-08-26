#include "Application.hpp"

#include <SDL3/SDL_vulkan.h>

#include <stdexcept>

Application::Application() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }

    window_ = SDL_CreateWindow(
        "CS490 glTF Viewer", 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        const char* error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error(error);
    }
}

Application::~Application() {
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

int Application::run() {
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // Renderer::drawFrame() will be called here after Vulkan initialization.
    }
    return 0;
}
