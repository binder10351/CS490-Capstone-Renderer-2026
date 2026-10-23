#pragma once
#include <SDL3/SDL.h>
#include "renderer/RenderSettings.hpp"

struct ImDrawData;

// Owns the UI context and SDL backend; Renderer owns its Vulkan backend.
class ViewerUi {
public:
    explicit ViewerUi(SDL_Window* window);
    ~ViewerUi();
    ViewerUi(const ViewerUi&) = delete;
    ViewerUi& operator=(const ViewerUi&) = delete;
    void processEvent(const SDL_Event& event);
    [[nodiscard]] bool captures(const SDL_Event& event) const;
    // Returns true when the user requests camera framing.
    bool build(RenderSettings& settings, bool& showGuides);
    [[nodiscard]] ImDrawData* drawData() const;
};
