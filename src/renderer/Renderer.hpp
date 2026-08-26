#pragma once

// Renderer will own Vulkan instance/device/swapchain state and all GPU resources.
class Renderer {
public:
    void drawFrame();
};
