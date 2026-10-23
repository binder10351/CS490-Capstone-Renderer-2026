#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mappedData = nullptr; // VMA-owned persistent mapping for host-updated buffers.
};
struct GpuMesh {
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
// Color textures use sRGB; normal and ORM data must stay linear.
enum class TextureSemantic {
    Color,
    Normal,
    OcclusionRoughnessMetallic
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
struct GpuMaterial {
    GpuBuffer uniformBuffer;
    GpuTexture normalMap, baseColorMap, metallicRoughnessMap, occlusionMap, emissiveMap;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};
