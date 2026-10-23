#define VMA_IMPLEMENTATION
#include "Renderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/packing.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const VkResult result, const char* action) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(action) + " failed (VkResult " + std::to_string(result) + ")");
    }
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Could not open shader: " + path.string());
    const auto bytes = static_cast<std::size_t>(file.tellg());
    if (bytes % sizeof(std::uint32_t) != 0) throw std::runtime_error("Invalid SPIR-V: " + path.string());
    std::vector<std::uint32_t> code(bytes / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
    return code;
}

VkShaderModule shaderModule(VkDevice device, const std::filesystem::path& path) {
    const auto code = readSpirv(path);
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size() * sizeof(std::uint32_t), .pCode = code.data()};
    VkShaderModule module;
    check(vkCreateShaderModule(device, &info, nullptr, &module), "Creating shader module");
    return module;
}

std::filesystem::path shaderDirectory() {
    const char* base = SDL_GetBasePath();
    if (!base) return std::filesystem::current_path() / "shaders";
    const auto directory = std::filesystem::path(base) / "shaders";
    return directory;
}

} // namespace

namespace {
MaterialUniforms materialUniforms(const SceneMaterial& material) {
    MaterialUniforms uniforms{};
    std::copy_n(glm::value_ptr(material.baseColor), 4, uniforms.baseColorFactor.begin());
    uniforms.metallicRoughnessOcclusion = {material.metallic, material.roughness, material.occlusionStrength, 0};
    uniforms.emissiveFactor = {material.emissive.x, material.emissive.y, material.emissive.z, 0};
    uniforms.normalParameters = {material.normalScale, material.normalMap.present() ? 1.0f : 0.0f, 0, 0};
    uniforms.alphaParameters = {static_cast<float>(material.alphaMode), material.alphaCutoff, material.doubleSided ? 1.0f : 0.0f, 0};
    return uniforms;
}
}

void Renderer::updateMaterialFactors(const Scene& scene) {
    if (scene.materials.size() != materials_.size()) throw std::runtime_error("Material count changed; rebuild the renderer");
    check(vkDeviceWaitIdle(device_), "Waiting to update material factors");
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        const auto uniforms = materialUniforms(scene.materials[i]);
        updateUniformBuffer(materials_[i].uniformBuffer, &uniforms, sizeof(uniforms));
    }
}

Renderer::Renderer(SDL_Window* window, const Scene& scene, bool enableUi) : uiEnabled_(enableUi), window_(window) {
    createInstance(); createSurface(); pickPhysicalDevice(); createDevice();
    createAllocator();
    createDescriptorSetLayouts();
    createUniformBuffers();
    createDescriptorPool(scene.materials.size());
    createDescriptorSets();
    createSwapchain(); createDepthResources(); createRenderPass(); createPipeline(); createFramebuffers();
    createCommandPool(); createCommandBuffers(); createSyncObjects();
    if (uiEnabled_) initializeUi();
    for (const auto& material : scene.materials) {
        materials_.push_back(createMaterial(materialUniforms(material), material, scene));
    }
    meshes_.resize(scene.primitives.size());
    for (std::size_t i = 0; i < scene.primitives.size(); ++i) {
        const auto& source = scene.primitives[i];
        auto& mesh = meshes_[i];
        mesh.vertexBuffer = createVertexBuffer(source.vertices.data(), source.vertices.size() * sizeof(Vertex));
        mesh.indexBuffer = createIndexBuffer(source.indices.data(), source.indices.size() * sizeof(std::uint32_t));
        mesh.vertexCount = static_cast<std::uint32_t>(source.vertices.size());
        mesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
    }
}

Renderer::~Renderer() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (uiInitialized_) { ImGui_ImplVulkan_Shutdown(); uiInitialized_ = false; }
        for (auto& mesh : meshes_) {
            destroyBuffer(mesh.vertexBuffer);
            destroyBuffer(mesh.indexBuffer);
        }
        for (auto& material : materials_) {
            destroyImage(material.normalMap);
            destroyImage(material.baseColorMap);
            destroyImage(material.metallicRoughnessMap);
            destroyImage(material.occlusionMap);
            destroyImage(material.emissiveMap);
            destroyBuffer(material.uniformBuffer);
        }
        for (std::uint32_t i = 0; i < maxFramesInFlight; ++i) {
            vkDestroySemaphore(device_, renderFinished_[i], nullptr);
            vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
            vkDestroyFence(device_, inFlight_[i], nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        destroySwapchain();
        if (descriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(
        device_,
        descriptorPool_,
        nullptr
    );

    descriptorPool_ = VK_NULL_HANDLE;
}

if (materialDescriptorSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(
        device_,
        materialDescriptorSetLayout_,
        nullptr
    );

    materialDescriptorSetLayout_ = VK_NULL_HANDLE;
}

if (frameDescriptorSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(
        device_,
        frameDescriptorSetLayout_,
        nullptr
    );

    frameDescriptorSetLayout_ = VK_NULL_HANDLE;
}
        for (auto& buffer : frameUniformBuffers_) {
    destroyBuffer(buffer);
}

for (auto& buffer : lightingUniformBuffers_) {
    destroyBuffer(buffer);
}
        destroyAllocator();
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
}

void Renderer::initializeUi() {
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = queueFamilies_.graphics;
    info.Queue = graphicsQueue_;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    info.PipelineInfoMain.RenderPass = renderPass_;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult result) { check(result, "UI Vulkan operation"); };
    if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("Could not initialize the Vulkan UI backend");
    uiInitialized_ = true;
}
void Renderer::beginUiFrame() {
    if (uiInitialized_) ImGui_ImplVulkan_NewFrame();
}

void Renderer::createInstance() {
    Uint32 count = 0;

    const char* const* sdlExtensions =
        SDL_Vulkan_GetInstanceExtensions(&count);

    if (!sdlExtensions) {
        throw std::runtime_error(SDL_GetError());
    }

    std::vector<const char*> extensions(
        sdlExtensions,
        sdlExtensions + count
    );

#ifdef __APPLE__
    extensions.push_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    );

    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    );
#endif

    const VkApplicationInfo app{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "CS490 glTF Viewer",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "CS490 Renderer",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_0
    };

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
    info.flags |=
        VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    check(
        vkCreateInstance(
            &info,
            nullptr,
            &instance_
        ),
        "Creating Vulkan instance"
    );
}

void Renderer::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) throw std::runtime_error(SDL_GetError());
}

Renderer::QueueFamilies Renderer::findQueueFamilies(VkPhysicalDevice device) const {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    QueueFamilies result;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) result.graphics = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present);
        if (present) result.present = i;
        if (result.complete()) break;
    }
    return result;
}

bool Renderer::supportsSwapchain(VkPhysicalDevice device) const {
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) { return std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME; });
    std::uint32_t formats = 0, modes = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formats, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modes, nullptr);
    return hasSwapchain && formats && modes;
}

void Renderer::pickPhysicalDevice() {
    std::uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "Enumerating GPUs");
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (const auto device : devices) {
        const QueueFamilies families = findQueueFamilies(device);
        if (families.complete() && supportsSwapchain(device)) { physicalDevice_ = device; queueFamilies_ = families; return; }
    }
    throw std::runtime_error("No GPU supports Vulkan presentation");
}

void Renderer::createDevice() {
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    queues.push_back({.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilies_.graphics, .queueCount = 1, .pQueuePriorities = &priority});
    if (queueFamilies_.present != queueFamilies_.graphics) queues.push_back({.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilies_.present, .queueCount = 1, .pQueuePriorities = &priority});
    #ifdef __APPLE__
    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"
    };
#else
    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
#endif
    const VkDeviceCreateInfo info{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = static_cast<std::uint32_t>(queues.size()), .pQueueCreateInfos = queues.data(), .enabledExtensionCount =
    static_cast<uint32_t>(std::size(extensions)), .ppEnabledExtensionNames = extensions};
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "Creating logical device");
    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
}


void Renderer::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;

    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

GpuBuffer Renderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags allocationFlags
) {
    GpuBuffer gpuBuffer{};
    gpuBuffer.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;

    VmaAllocationInfo createdAllocation{};
    if (vmaCreateBuffer(
            allocator_,
            &bufferInfo,
            &allocationInfo,
            &gpuBuffer.buffer,
            &gpuBuffer.allocation,
            &createdAllocation) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU buffer");
    }

    gpuBuffer.mappedData = createdAllocation.pMappedData;
    return gpuBuffer;
}
void Renderer::destroyBuffer(GpuBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(
            allocator_,
            buffer.buffer,
            buffer.allocation
        );

        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size = 0;
        buffer.mappedData = nullptr;
    }
}
void Renderer::copyBuffer(
    VkBuffer source,
    VkBuffer destination,
    VkDeviceSize size
) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
            device_,
            &allocInfo,
            &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate copy command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("Failed to begin copy command buffer");
    }

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(
        commandBuffer,
        source,
        destination,
        1,
        &copyRegion
    );

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = destination;
    barrier.size = size;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("Failed to end copy command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(
            graphicsQueue_,
            1,
            &submitInfo,
            VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("Failed to submit copy command");
    }

    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(
        device_,
        commandPool_,
        1,
        &commandBuffer
    );
}
GpuBuffer Renderer::uploadBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags finalUsage
) {
    GpuBuffer stagingBuffer = createBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    void* mappedData = nullptr;

if (vmaMapMemory(
        allocator_,
        stagingBuffer.allocation,
        &mappedData) != VK_SUCCESS) {
    destroyBuffer(stagingBuffer);
    throw std::runtime_error("Failed to map staging buffer memory");
}
std::memcpy(
    mappedData,
    data,
    static_cast<std::size_t>(size)
);

if (vmaFlushAllocation(
        allocator_,
        stagingBuffer.allocation,
        0,
        size) != VK_SUCCESS) {
    vmaUnmapMemory(
        allocator_,
        stagingBuffer.allocation
    );

    destroyBuffer(stagingBuffer);

    throw std::runtime_error(
        "Failed to flush staging buffer memory"
    );
}

vmaUnmapMemory(
    allocator_,
    stagingBuffer.allocation
);
GpuBuffer gpuBuffer = createBuffer(
    size,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | finalUsage,
    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
);
copyBuffer(
    stagingBuffer.buffer,
    gpuBuffer.buffer,
    size
);
destroyBuffer(stagingBuffer);

    return gpuBuffer;
}
GpuBuffer Renderer::createVertexBuffer(
    const void* vertexData,
    VkDeviceSize size
) {
    return uploadBuffer(
        vertexData,
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );
}
GpuBuffer Renderer::createIndexBuffer(
    const void* indexData,
    VkDeviceSize size
) {
    return uploadBuffer(
        indexData,
        size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    );
}
GpuBuffer Renderer::createUniformBuffer(VkDeviceSize size) {
    return createBuffer(
        size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
}

void Renderer::updateUniformBuffer(GpuBuffer& buffer, const void* data, VkDeviceSize size) {
    if (size > buffer.size) {
        throw std::runtime_error("Uniform data is larger than the uniform buffer");
    }
    if (buffer.mappedData == nullptr) {
        throw std::runtime_error("Uniform buffer has no persistent mapping");
    }
    std::memcpy(buffer.mappedData, data, static_cast<std::size_t>(size));
    // Required for noncoherent host memory; VMA handles coherent allocations.
    check(vmaFlushAllocation(allocator_, buffer.allocation, 0, size),
          "Flushing uniform buffer memory");
}

void Renderer::createUniformBuffers() {
    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i) {
        frameUniformBuffers_[i] =
            createUniformBuffer(sizeof(FrameUniforms));

        lightingUniformBuffers_[i] =
            createUniformBuffer(sizeof(LightingUniforms));
    }
}

GpuMaterial Renderer::createMaterial(
    const MaterialUniforms& material, const SceneMaterial& source, const Scene& scene
) {
    GpuMaterial gpuMaterial{};

    gpuMaterial.uniformBuffer =
        createUniformBuffer(sizeof(MaterialUniforms));

    updateUniformBuffer(
        gpuMaterial.uniformBuffer,
        &material,
        sizeof(MaterialUniforms)
    );

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts =
        &materialDescriptorSetLayout_;

    if (vkAllocateDescriptorSets(
            device_,
            &allocateInfo,
            &gpuMaterial.descriptorSet) != VK_SUCCESS) {
        destroyBuffer(gpuMaterial.uniformBuffer);

        throw std::runtime_error(
            "Failed to allocate material descriptor set"
        );
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer =
        gpuMaterial.uniformBuffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MaterialUniforms);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet =
        gpuMaterial.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo =
        &bufferInfo;

    const std::array<std::uint8_t, 4> flatNormal{128, 128, 255, 255}, white{255, 255, 255, 255};
    const std::array<const SceneTexture*, 5> sources{&source.normalMap, &source.baseColorMap,
        &source.metallicRoughnessMap, &source.occlusionMap, &source.emissiveMap};
    const std::array<GpuTexture*, 5> textures{&gpuMaterial.normalMap, &gpuMaterial.baseColorMap,
        &gpuMaterial.metallicRoughnessMap, &gpuMaterial.occlusionMap, &gpuMaterial.emissiveMap};
    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0] = descriptorWrite;
    try {
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const auto& image = *sources[i];
            const auto semantic = i == 0 ? TextureSemantic::Normal : (i == 1 || i == 4) ? TextureSemantic::Color :
                TextureSemantic::OcclusionRoughnessMetallic;
            const auto& fallback = i == 0 ? flatNormal : white;
            if (!image.present()) *textures[i] = uploadTexture(fallback.data(), 1, 1, semantic);
            else {
                const auto& resource = scene.textures.at(image.texture);
                const auto& pixels = scene.images.at(resource.image);
                const auto sampler = resource.sampler < 0 ? SceneSampler{} : scene.samplers.at(resource.sampler);
                *textures[i] = uploadTexture(pixels.rgba.data(), pixels.width, pixels.height, semantic, sampler, pixels.bits);
            }
            imageInfos[i] = {textures[i]->sampler, textures[i]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto& write = writes[i+1];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = gpuMaterial.descriptorSet;
            write.dstBinding = static_cast<std::uint32_t>(i+1);
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos[i];
        }
    } catch (...) {
        for (auto* texture : textures) destroyImage(*texture);
        destroyBuffer(gpuMaterial.uniformBuffer);
        throw;
    }
    vkUpdateDescriptorSets(
        device_,
        static_cast<std::uint32_t>(writes.size()),
        writes.data(),
        0,
        nullptr
    );

    return gpuMaterial;
}
void Renderer::createDescriptorSetLayouts() {
    std::array<VkDescriptorSetLayoutBinding, 2> frameBindings{};

    frameBindings[0].binding = 0;
    frameBindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBindings[0].descriptorCount = 1;
    frameBindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT;

    frameBindings[1].binding = 1;
    frameBindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBindings[1].descriptorCount = 1;
    frameBindings[1].stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
    frameLayoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameLayoutInfo.bindingCount =
        static_cast<uint32_t>(frameBindings.size());
    frameLayoutInfo.pBindings = frameBindings.data();

    if (vkCreateDescriptorSetLayout(
            device_,
            &frameLayoutInfo,
            nullptr,
            &frameDescriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create frame descriptor set layout"
        );
    }

    VkDescriptorSetLayoutBinding materialBinding{};
    materialBinding.binding = 0;
    materialBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBinding.descriptorCount = 1;
    materialBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    std::array<VkDescriptorSetLayoutBinding, 6> materialBindings{};
    materialBindings[0] = materialBinding;
    for (std::uint32_t i = 1; i < materialBindings.size(); ++i) {
        materialBindings[i].binding = i;
        materialBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[i].descriptorCount = 1;
        materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    materialLayoutInfo.bindingCount = static_cast<std::uint32_t>(materialBindings.size());
    materialLayoutInfo.pBindings = materialBindings.data();

    if (vkCreateDescriptorSetLayout(
            device_,
            &materialLayoutInfo,
            nullptr,
            &materialDescriptorSetLayout_) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(
            device_,
            frameDescriptorSetLayout_,
            nullptr
        );

        frameDescriptorSetLayout_ = VK_NULL_HANDLE;

        throw std::runtime_error(
            "Failed to create material descriptor set layout"
        );
    }
}
void Renderer::createDescriptorPool(std::size_t materialCount) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<std::uint32_t>(materialCount) + maxFramesInFlight * 2;
    const std::array<VkDescriptorPoolSize, 2> poolSizes{{poolSize,
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(materialCount) * 5}}};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<std::uint32_t>(materialCount) + maxFramesInFlight;

    if (vkCreateDescriptorPool(
            device_,
            &poolInfo,
            nullptr,
            &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create descriptor pool"
        );
    }
}
void Renderer::createDescriptorSets() {
    std::array<VkDescriptorSetLayout, maxFramesInFlight> layouts{};
    layouts.fill(frameDescriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = maxFramesInFlight;
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(
            device_,
            &allocateInfo,
            frameDescriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate frame descriptor sets"
        );
    }

    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i) {
        VkDescriptorBufferInfo frameBufferInfo{};
        frameBufferInfo.buffer = frameUniformBuffers_[i].buffer;
        frameBufferInfo.offset = 0;
        frameBufferInfo.range = sizeof(FrameUniforms);

        VkDescriptorBufferInfo lightingBufferInfo{};
        lightingBufferInfo.buffer =
            lightingUniformBuffers_[i].buffer;
        lightingBufferInfo.offset = 0;
        lightingBufferInfo.range = sizeof(LightingUniforms);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet =
            frameDescriptorSets_[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo =
            &frameBufferInfo;

        descriptorWrites[1].sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet =
            frameDescriptorSets_[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo =
            &lightingBufferInfo;

        vkUpdateDescriptorSets(
            device_,
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
}
GpuTexture Renderer::createImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkFormat format,
    VkImageUsageFlags usage
) {
    GpuTexture texture{};

    texture.width = width;
    texture.height = height;
    texture.mipLevels = mipLevels;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(
            allocator_,
            &imageInfo,
            &allocationInfo,
            &texture.image,
            &texture.allocation,
            nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU image");
    }

    return texture;
}
void Renderer::destroyImage(GpuTexture& texture) {
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }

    if (texture.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, texture.imageView, nullptr);
        texture.imageView = VK_NULL_HANDLE;
    }

    if (texture.image != VK_NULL_HANDLE) {
        vmaDestroyImage(
            allocator_,
            texture.image,
            texture.allocation
        );

        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
    }

    texture.width = 0;
    texture.height = 0;
    texture.mipLevels = 1;
}
void Renderer::transitionImageLayout(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels
) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
            device_,
            &allocInfo,
            &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate image transition command buffer"
        );
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error(
            "Failed to begin image transition command buffer"
        );
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = image;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    ) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (
        oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    ) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::invalid_argument(
            "Unsupported image layout transition"
        );
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to end image transition command buffer"
        );
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(
            graphicsQueue_,
            1,
            &submitInfo,
            VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to submit image transition command"
        );
    }

    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(
        device_,
        commandPool_,
        1,
        &commandBuffer
    );
}
void Renderer::copyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height
) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
            device_,
            &allocInfo,
            &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate buffer-to-image command buffer"
        );
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(
            commandBuffer,
            &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to begin buffer-to-image command buffer"
        );
    }

    VkBufferImageCopy region{};

    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};

    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to end buffer-to-image command buffer"
        );
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(
            graphicsQueue_,
            1,
            &submitInfo,
            VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to submit buffer-to-image copy command"
        );
    }

    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(
        device_,
        commandPool_,
        1,
        &commandBuffer
    );
}
GpuTexture Renderer::uploadTexture(
    const void* pixelData,
    uint32_t width,
    uint32_t height,
    TextureSemantic semantic,
    const SceneSampler& settings,
    int bits
) {
    VkFormat format;
    switch (semantic) {
    case TextureSemantic::Color:
        format = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case TextureSemantic::Normal:
    case TextureSemantic::OcclusionRoughnessMetallic:
        format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::runtime_error("Unknown texture semantic");
    }

    if (pixelData == nullptr || width == 0 || height == 0 || (bits != 8 && bits != 16))
        throw std::runtime_error("Cannot upload an empty or unsupported texture");
    // Vulkan has no 16-bit sRGB format. Decode high-precision color images to
    // linear half floats before upload; data images use the same linear format.
    std::vector<std::uint16_t> halfPixels;
    if (bits == 16) {
        const auto count = static_cast<std::size_t>(width) * height * 4;
        halfPixels.reserve(count);
        const auto* bytes = static_cast<const std::uint8_t*>(pixelData);
        for (std::size_t i = 0; i < count; ++i) {
            std::uint16_t channel;
            std::memcpy(&channel, bytes + i * 2, 2);
            float value = channel / 65535.0f;
            if (semantic == TextureSemantic::Color && i % 4 != 3)
                value = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            halfPixels.push_back(glm::packHalf1x16(value));
        }
        pixelData = halfPixels.data();
        format = VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4 * (bits / 8);

    GpuBuffer stagingBuffer = createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    void* mappedData = nullptr;

    if (vmaMapMemory(
            allocator_,
            stagingBuffer.allocation,
            &mappedData) != VK_SUCCESS) {
        destroyBuffer(stagingBuffer);
        throw std::runtime_error(
            "Failed to map texture staging buffer"
        );
    }

    std::memcpy(
        mappedData,
        pixelData,
        static_cast<std::size_t>(imageSize)
    );

    if (vmaFlushAllocation(
            allocator_,
            stagingBuffer.allocation,
            0,
            imageSize) != VK_SUCCESS) {
        vmaUnmapMemory(
            allocator_,
            stagingBuffer.allocation
        );

        destroyBuffer(stagingBuffer);

        throw std::runtime_error(
            "Failed to flush texture staging buffer"
        );
    }

    vmaUnmapMemory(
        allocator_,
        stagingBuffer.allocation
    );

    GpuTexture texture = createImage(
        width,
        height,
        mipLevels,
        format,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT
    );

    transitionImageLayout(
        texture.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels
    );

    copyBufferToImage(
        stagingBuffer.buffer,
        texture.image,
        width,
        height
    );

  generateMipmaps(
    texture.image,
    format,
    static_cast<int32_t>(width),
    static_cast<int32_t>(height),
    mipLevels
);
    texture.imageView = createImageView(
    texture.image,
    format,
    texture.mipLevels
);
texture.sampler = createTextureSampler(
    texture.mipLevels, settings
);

    destroyBuffer(stagingBuffer);

    return texture;
}
VkImageView Renderer::createImageView(
    VkImage image,
    VkFormat format,
    uint32_t mipLevels
) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;

    if (vkCreateImageView(
            device_,
            &viewInfo,
            nullptr,
            &imageView) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create texture image view"
        );
    }

    return imageView;
}
VkSampler Renderer::createTextureSampler(
    uint32_t mipLevels, const SceneSampler& settings
) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = settings.magFilter == 9728 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter = (settings.minFilter == 9728 || settings.minFilter == 9984 || settings.minFilter == 9986)
        ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    auto addressMode = [](int wrap) {
        return wrap == 33071 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE :
            wrap == 33648 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    samplerInfo.addressModeU = addressMode(settings.wrapS);
    samplerInfo.addressModeV = addressMode(settings.wrapT);
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = settings.minFilter == 9984 || settings.minFilter == 9985
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = settings.minFilter == 9728 || settings.minFilter == 9729
        ? 0.0f : static_cast<float>(mipLevels - 1);

    VkSampler sampler = VK_NULL_HANDLE;

    if (vkCreateSampler(
            device_,
            &samplerInfo,
            nullptr,
            &sampler) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create texture sampler"
        );
    }

    return sampler;
}
void Renderer::generateMipmaps(
    VkImage image,
    VkFormat imageFormat,
    int32_t width,
    int32_t height,
    uint32_t mipLevels
) {
    VkFormatProperties formatProperties{};

vkGetPhysicalDeviceFormatProperties(
    physicalDevice_,
    imageFormat,
    &formatProperties
);

if (!(formatProperties.optimalTilingFeatures &
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    throw std::runtime_error(
        "Texture image format does not support linear blitting"
    );
}
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
            device_,
            &allocInfo,
            &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate mipmap command buffer"
        );
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(
            commandBuffer,
            &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to begin mipmap command buffer"
        );
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; ++i) {
        barrier.subresourceRange.baseMipLevel = i - 1;

        barrier.oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        barrier.newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        barrier.srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;

        barrier.dstAccessMask =
            VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        VkImageBlit blit{};

        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {
            mipWidth,
            mipHeight,
            1
        };

        blit.srcSubresource.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;

        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {
            mipWidth > 1 ? mipWidth / 2 : 1,
            mipHeight > 1 ? mipHeight / 2 : 1,
            1
        };

        blit.dstSubresource.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;

        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        barrier.oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        barrier.newLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        barrier.srcAccessMask =
            VK_ACCESS_TRANSFER_READ_BIT;

        barrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        if (mipWidth > 1) {
            mipWidth /= 2;
        }

        if (mipHeight > 1) {
            mipHeight /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel =
        mipLevels - 1;

    barrier.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrier.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to end mipmap command buffer"
        );
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(
            graphicsQueue_,
            1,
            &submitInfo,
            VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            device_,
            commandPool_,
            1,
            &commandBuffer
        );

        throw std::runtime_error(
            "Failed to submit mipmap command"
        );
    }

    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(
        device_,
        commandPool_,
        1,
        &commandBuffer
    );
}

void Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities), "Reading surface capabilities");
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, formats.data());
    const auto preferred = std::find_if(formats.begin(), formats.end(), [](const auto& f) { return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
    const VkSurfaceFormatKHR format = preferred == formats.end() ? formats.front() : *preferred;
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        swapchainExtent_ = capabilities.currentExtent;
    } else {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        swapchainExtent_ = {std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width), std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount) imageCount = capabilities.maxImageCount;
    const std::array<std::uint32_t, 2> families{queueFamilies_.graphics, queueFamilies_.present};
    const bool separateQueues = queueFamilies_.graphics != queueFamilies_.present;
    const VkSwapchainCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface_, .minImageCount = imageCount, .imageFormat = format.format, .imageColorSpace = format.colorSpace, .imageExtent = swapchainExtent_, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = separateQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE, .queueFamilyIndexCount = separateQueues ? 2u : 0u, .pQueueFamilyIndices = separateQueues ? families.data() : nullptr, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "Creating vsync swapchain");
    swapchainFormat_ = format.format;
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
    swapchainImageViews_.resize(imageCount);
    for (std::uint32_t i = 0; i < imageCount; ++i) {
        const VkImageViewCreateInfo view{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchainImages_[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swapchainFormat_, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        check(vkCreateImageView(device_, &view, nullptr, &swapchainImageViews_[i]), "Creating swapchain image view");
    }
}

void Renderer::createDepthResources() {
    depthFormat_ = VK_FORMAT_UNDEFINED;
    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, candidate, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = candidate;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No supported depth attachment format");
    }

    depthImages_.resize(swapchainImages_.size());
    try {
        for (auto& depth : depthImages_) {
            depth = createImage(swapchainExtent_.width, swapchainExtent_.height, 1,
                depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = depth.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = depthFormat_;
            view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device_, &view, nullptr, &depth.imageView), "Creating depth image view");
        }
    } catch (...) {
        for (auto& depth : depthImages_) destroyImage(depth);
        depthImages_.clear();
        throw;
    }
    // The render pass performs the initial layout transition and clears depth.
}

void Renderer::createRenderPass() {
    const VkAttachmentDescription color{.format = swapchainFormat_, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    const VkAttachmentReference reference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const std::array<VkAttachmentDescription, 2> attachments{color, depth};
    const VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference, .pDepthStencilAttachment = &depthReference};
    const VkPipelineStageFlags attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkSubpassDependency dependency{.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = attachmentStages, .dstStageMask = attachmentStages,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    const VkRenderPassCreateInfo info{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = static_cast<uint32_t>(attachments.size()), .pAttachments = attachments.data(), .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
    check(vkCreateRenderPass(device_, &info, nullptr, &renderPass_), "Creating render pass");
}

void Renderer::createPipeline() {
    const auto directory = shaderDirectory();
    const VkShaderModule vertex = shaderModule(device_, directory / "gltf_mesh.vert.spv");
    const VkShaderModule fragment = shaderModule(device_, directory / "gltf_ggx.frag.spv");
    const VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"}, {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 9> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, baseColorUv)},
        {5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, metallicRoughnessUv)},
        {6, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, occlusionUv)},
        {7, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, emissiveUv)},
        {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}
    }};
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()};
    const VkPipelineInputAssemblyStateCreateInfo assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rasterizer{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment};
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = states};
    const std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts = {
        frameDescriptorSetLayout_, materialDescriptorSetLayout_
    };
    const VkPushConstantRange objectRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ObjectUniforms)};
    const VkPipelineLayoutCreateInfo layout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &objectRange
    };
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Creating pipeline layout");
    const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vertexInput, .pInputAssemblyState = &assembly, .pViewportState = &viewport, .pRasterizationState = &rasterizer, .pMultisampleState = &multisample, .pDepthStencilState = &depthStencil, .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = pipelineLayout_, .renderPass = renderPass_};
    VkResult result = VK_SUCCESS;
    for (std::size_t i = 0; i < pipelines_.size(); ++i) {
        rasterizer.frontFace = i & 1 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.cullMode = i & 2 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        const bool transparent = (i & 4) != 0;
        depthStencil.depthWriteEnable = transparent ? VK_FALSE : VK_TRUE;
        blendAttachment.blendEnable = transparent ? VK_TRUE : VK_FALSE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines_[i]);
        if (result != VK_SUCCESS) break;
    }
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    check(result, "Creating graphics pipelines");
}

void Renderer::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (std::size_t i = 0; i < framebuffers_.size(); ++i) {
        const std::array<VkImageView, 2> attachments{swapchainImageViews_[i], depthImages_[i].imageView};
        const VkFramebufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = renderPass_, .attachmentCount = static_cast<uint32_t>(attachments.size()), .pAttachments = attachments.data(), .width = swapchainExtent_.width, .height = swapchainExtent_.height, .layers = 1};
        check(vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]), "Creating framebuffer");
    }
}

void Renderer::createCommandPool() {
    const VkCommandPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = queueFamilies_.graphics};
    check(vkCreateCommandPool(device_, &info, nullptr, &commandPool_), "Creating command pool");
}

void Renderer::createCommandBuffers() {
    const VkCommandBufferAllocateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool_, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = maxFramesInFlight};
    check(vkAllocateCommandBuffers(device_, &info, commandBuffers_.data()), "Allocating command buffers");
}

void Renderer::createSyncObjects() {
    const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i) {
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_[i]), "Creating image semaphore");
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_[i]), "Creating render semaphore");
        check(vkCreateFence(device_, &fence, nullptr, &inFlight_[i]), "Creating frame fence");
    }
}

void Renderer::recordCommandBuffer(VkCommandBuffer buffer, std::uint32_t imageIndex, const Scene& scene, const Camera& camera, bool showGuides, ImDrawData* uiDrawData) {
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(buffer, &begin), "Beginning command buffer");
    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0.015f, 0.02f, 0.05f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    const VkRenderPassBeginInfo pass{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass_, .framebuffer = framebuffers_[imageIndex], .renderArea = {{0, 0}, swapchainExtent_}, .clearValueCount = static_cast<uint32_t>(clears.size()), .pClearValues = clears.data()};
    vkCmdBeginRenderPass(buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{.width = static_cast<float>(swapchainExtent_.width), .height = static_cast<float>(swapchainExtent_.height), .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetViewport(buffer, 0, 1, &viewport); vkCmdSetScissor(buffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
        &frameDescriptorSets_[currentFrame_], 0, nullptr);
    struct Draw { std::size_t node, primitive; float depth; bool transparent; };
    std::vector<Draw> draws;
    std::vector<ObjectUniforms> objects(scene.nodes.size());
    const auto view = camera.view();
    for (std::size_t n = 0; n < scene.nodes.size(); ++n) {
        const auto& node = scene.nodes[n];
        if (node.guide && !showGuides) continue;
        const float determinant = glm::determinant(glm::mat3(node.world));
        if (!std::isfinite(determinant) || determinant == 0.0f) continue;
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(node.world)));
        auto& object = objects[n];
        std::copy_n(glm::value_ptr(node.world), 16, object.model.begin());
        for (std::size_t column = 0; column < 3; ++column)
            std::copy_n(glm::value_ptr(normalMatrix[column]), 3, object.normalColumns[column].begin());
        object.normalColumns[0][3] = determinant < 0.0f ? -1.0f : 1.0f;
        for (auto index : node.primitives) {
            const auto& primitive = scene.primitives.at(index);
            const auto& material = scene.materials.at(primitive.material);
            const auto center = (primitive.bounds.min + primitive.bounds.max) * 0.5f;
            const float depth = -(view * node.world * glm::vec4(center, 1)).z;
            draws.push_back({n, index, depth, material.alphaMode == AlphaMode::Blend});
        }
    }
    // Opaque/masked draws populate depth first; blended instances are sorted in
    // camera space. Intersecting transparent triangles still need OIT or splitting.
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
        if (a.transparent != b.transparent) return !a.transparent;
        return a.transparent && a.depth > b.depth;
    });
    for (const auto& draw : draws) {
        const auto& object = objects[draw.node];
        vkCmdPushConstants(buffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(object), &object);
        const auto& primitive = scene.primitives.at(draw.primitive);
        const auto& source = scene.materials.at(primitive.material);
        const std::size_t pipeline = (object.normalColumns[0][3] < 0 ? 1 : 0) | (source.doubleSided ? 2 : 0) | (draw.transparent ? 4 : 0);
        vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[pipeline]);
        const auto& mesh = meshes_.at(draw.primitive);
        const auto& material = materials_.at(primitive.material);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(buffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(buffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1,
            &material.descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(buffer, mesh.indexCount, 1, 0, 0, 0);
    }
    if (uiInitialized_ && uiDrawData) ImGui_ImplVulkan_RenderDrawData(uiDrawData, buffer);
    vkCmdEndRenderPass(buffer);
    check(vkEndCommandBuffer(buffer), "Ending command buffer");
}

void Renderer::drawFrame(const Scene& scene, const Camera& camera, bool showGuides,
    const RenderSettings& settings, ImDrawData* uiDrawData) {
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GetWindowSizeInPixels(window_, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) return;
    // Reuse this frame slot only after its previous GPU work has finished.
    check(vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX),
          "Waiting for frame fence");
    std::uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) check(acquire, "Acquiring swapchain image");
    check(vkResetFences(device_, 1, &inFlight_[currentFrame_]), "Resetting frame fence");
    check(vkResetCommandBuffer(commandBuffers_[currentFrame_], 0), "Resetting command buffer");
    FrameUniforms frame{};
    const auto viewProjection = camera.projection(static_cast<float>(swapchainExtent_.width) / swapchainExtent_.height) * camera.view();
    std::copy_n(glm::value_ptr(viewProjection), 16, frame.viewProjection.begin());
    updateUniformBuffer(frameUniformBuffers_[currentFrame_], &frame, sizeof(frame));
    LightingUniforms lighting{};
    lighting.cameraWorldPosition = {camera.position.x, camera.position.y, camera.position.z, 1};
    lighting.ambientColor = {settings.ambientIntensity, settings.ambientIntensity, settings.ambientIntensity, 1};
    lighting.lights[0].positionOrDirection = {settings.worldLightDirection.x, settings.worldLightDirection.y, settings.worldLightDirection.z, 0};
    lighting.lights[0].colorIntensity = {settings.worldLightColor.x, settings.worldLightColor.y, settings.worldLightColor.z, settings.worldLightIntensity};
    lighting.lightCount = {settings.worldLightEnabled && glm::length(settings.worldLightDirection) > 1e-6f ? 1.0f : 0.0f, 0, 0, 0};
    lighting.textureOptions = {settings.normalMapping ? 1.0f : 0.0f, settings.baseColorTextures ? 1.0f : 0.0f,
        settings.metallicRoughnessTextures ? 1.0f : 0.0f, settings.occlusion ? 1.0f : 0.0f};
    lighting.shadingOptions = {settings.emissive ? 1.0f : 0.0f, settings.vertexColors ? 1.0f : 0.0f,
        settings.diffuse ? 1.0f : 0.0f, settings.specular ? 1.0f : 0.0f};
    updateUniformBuffer(lightingUniformBuffers_[currentFrame_], &lighting, sizeof(lighting));
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, scene, camera, showGuides, uiDrawData);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &imageAvailable_[currentFrame_], .pWaitDstStageMask = &waitStage, .commandBufferCount = 1, .pCommandBuffers = &commandBuffers_[currentFrame_], .signalSemaphoreCount = 1, .pSignalSemaphores = &renderFinished_[currentFrame_]};
    check(vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_[currentFrame_]), "Submitting frame");
    const VkPresentInfoKHR present{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &renderFinished_[currentFrame_], .swapchainCount = 1, .pSwapchains = &swapchain_, .pImageIndices = &imageIndex};
    const VkResult result = vkQueuePresentKHR(presentQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) recreateSwapchain();
    else check(result, "Presenting frame");
    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight;
}

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width == 0 || height == 0) return;
    vkDeviceWaitIdle(device_);
    if (uiInitialized_) { ImGui_ImplVulkan_Shutdown(); uiInitialized_ = false; }
    destroySwapchain(); createSwapchain(); createDepthResources(); createRenderPass(); createPipeline(); createFramebuffers();
    if (uiEnabled_) initializeUi();
}

void Renderer::destroySwapchain() {
    for (const auto framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    framebuffers_.clear();
    for (auto& depth : depthImages_) destroyImage(depth);
    depthImages_.clear();
    for (auto pipeline : pipelines_) if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    pipelines_.fill(VK_NULL_HANDLE); pipelineLayout_ = VK_NULL_HANDLE; renderPass_ = VK_NULL_HANDLE;
    for (const auto view : swapchainImageViews_) vkDestroyImageView(device_, view, nullptr);
    swapchainImageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void Renderer::destroyAllocator() {
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
}
