#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
};