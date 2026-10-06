#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "GpuResources.hpp"

#include <array>
#include <cstdint>
#include <vector>

class Renderer {
public:
    explicit Renderer(SDL_Window* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame();

private:
    static constexpr std::uint32_t maxFramesInFlight = 2;
    struct QueueFamilies {
        std::uint32_t graphics = UINT32_MAX;
        std::uint32_t present = UINT32_MAX;
        [[nodiscard]] bool complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    GpuBuffer createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags allocationFlags = 0
);
void destroyBuffer(GpuBuffer& buffer);
void copyBuffer(
    VkBuffer source,
    VkBuffer destination,
    VkDeviceSize size
);
GpuBuffer uploadBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags finalUsage
);
GpuBuffer createVertexBuffer(
    const void* vertexData,
    VkDeviceSize size
);
GpuBuffer createIndexBuffer(
    const void* indexData,
    VkDeviceSize size
);
GpuTexture createImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkFormat format,
    VkImageUsageFlags usage
);
void destroyImage(GpuTexture& texture);
void transitionImageLayout(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels
);
void copyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height
);

GpuTexture uploadTexture(
    const void* pixelData,
    uint32_t width,
    uint32_t height
);
    void createSwapchain();
    void destroySwapchain();
    void destroyAllocator();
    void createRenderPass();
    void createPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void recreateSwapchain();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex);
    [[nodiscard]] QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool supportsSwapchain(VkPhysicalDevice device) const;

    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilies queueFamilies_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers_{};
    std::array<VkSemaphore, maxFramesInFlight> imageAvailable_{};
    std::array<VkSemaphore, maxFramesInFlight> renderFinished_{};
    std::array<VkFence, maxFramesInFlight> inFlight_{};
    std::uint32_t currentFrame_ = 0;
};
