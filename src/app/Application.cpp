#include "Application.hpp"
#include "ViewerUi.hpp"
#include "renderer/Renderer.hpp"
#include "scene/GltfLoader.hpp"

#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <string>

namespace {
void reportWarnings(const Scene& scene) {
    for (const auto& warning : scene.warnings) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", warning.c_str());
}
}

Application::Application(const std::filesystem::path& modelPath) {
    scene_ = modelPath.empty() ? Scene::cube() : GltfLoader{}.load(modelPath);
    reportWarnings(scene_);
    scene_.addGuides();
    controller_.frame(scene_.worldBounds(), 1280.0f / 720.0f);
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

    try {
        ui_ = std::make_unique<ViewerUi>(window_);
        renderer_ = std::make_unique<Renderer>(window_, scene_, true);
    } catch (...) {
        ui_.reset();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw;
    }
    SDL_SetWindowTitle(window_, "glTF Viewer | Drop .gltf/.glb to open | Drag: orbit | Shift-drag/Middle: pan | Wheel: dolly | F: frame | G: guides");
}

void Application::loadModel(const std::filesystem::path& modelPath) {
    Scene nextScene;
    try {
        auto extension = modelPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".gltf" && extension != ".glb") {
            throw std::runtime_error("Choose a .gltf or .glb model file.");
        }
        nextScene = GltfLoader{}.load(modelPath);
        reportWarnings(nextScene);
        nextScene.addGuides();
    } catch (const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not load model", error.what(), window_);
        return;
    }

    // Destroy the old swapchain before creating another for the same window.
    // Invalid files are rejected above, leaving the current model intact.
    renderer_.reset();
    scene_ = std::move(nextScene);
    renderer_ = std::make_unique<Renderer>(window_, scene_, true);
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    controller_.frame(scene_.worldBounds(), height > 0 ? static_cast<float>(width) / height : 1.0f);
}

Application::~Application() {
    renderer_.reset();
    ui_.reset();
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
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window_, &width, &height);
            ui_->processEvent(event);
            const bool captured = ui_->captures(event);
            if (!captured) controller_.handleEvent(event, height);
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                const std::string utf8Path(event.drop.data);
                loadModel(std::filesystem::path(std::u8string(utf8Path.begin(), utf8Path.end())));
            }
            if (!captured && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_F) controller_.frame(scene_.worldBounds(), height > 0 ? static_cast<float>(width) / height : 1.0f);
                if (event.key.key == SDLK_G) showGuides_ = !showGuides_;
            }
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        if (!running) break;
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(16);
            continue;
        }
        renderer_->beginUiFrame();
        if (ui_->build(settings_, showGuides_)) {
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window_, &width, &height);
            controller_.frame(scene_.worldBounds(), height > 0 ? static_cast<float>(width) / height : 1.0f);
        }
        scene_.updateWorldTransforms();
        renderer_->drawFrame(scene_, controller_.camera, showGuides_, settings_, ui_->drawData());
    }
    return 0;
}
