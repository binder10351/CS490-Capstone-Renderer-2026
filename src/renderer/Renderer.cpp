#include "Renderer.hpp"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
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

Renderer::Renderer(SDL_Window* window) : window_(window) {
    createInstance(); createSurface(); pickPhysicalDevice(); createDevice();
    createSwapchain(); createRenderPass(); createPipeline(); createFramebuffers();
    createCommandPool(); createCommandBuffers(); createSyncObjects();
}

Renderer::~Renderer() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        for (std::uint32_t i = 0; i < maxFramesInFlight; ++i) {
            vkDestroySemaphore(device_, renderFinished_[i], nullptr);
            vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
            vkDestroyFence(device_, inFlight_[i], nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        destroySwapchain();
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
}

void Renderer::createInstance() {
    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions) throw std::runtime_error(SDL_GetError());
    const VkApplicationInfo app{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "CS490 glTF Viewer", .applicationVersion = VK_MAKE_VERSION(0, 1, 0), .pEngineName = "CS490 Renderer", .engineVersion = VK_MAKE_VERSION(0, 1, 0), .apiVersion = VK_API_VERSION_1_0};
    const VkInstanceCreateInfo info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = count, .ppEnabledExtensionNames = extensions};
    check(vkCreateInstance(&info, nullptr, &instance_), "Creating Vulkan instance");
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
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo info{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = static_cast<std::uint32_t>(queues.size()), .pQueueCreateInfos = queues.data(), .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "Creating logical device");
    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
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

void Renderer::createRenderPass() {
    const VkAttachmentDescription color{.format = swapchainFormat_, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    const VkAttachmentReference reference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference};
    const VkSubpassDependency dependency{.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    const VkRenderPassCreateInfo info{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
    check(vkCreateRenderPass(device_, &info, nullptr, &renderPass_), "Creating render pass");
}

void Renderer::createPipeline() {
    const auto directory = shaderDirectory();
    const VkShaderModule vertex = shaderModule(device_, directory / "fullscreen.vert.spv");
    const VkShaderModule fragment = shaderModule(device_, directory / "fullscreen.frag.spv");
    const VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"}, {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    const VkPipelineVertexInputStateCreateInfo vertexInput{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo rasterizer{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment};
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = states};
    const VkPipelineLayoutCreateInfo layout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Creating pipeline layout");
    const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vertexInput, .pInputAssemblyState = &assembly, .pViewportState = &viewport, .pRasterizationState = &rasterizer, .pMultisampleState = &multisample, .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = pipelineLayout_, .renderPass = renderPass_};
    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    check(result, "Creating graphics pipeline");
}

void Renderer::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (std::size_t i = 0; i < framebuffers_.size(); ++i) {
        const VkImageView attachment = swapchainImageViews_[i];
        const VkFramebufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = renderPass_, .attachmentCount = 1, .pAttachments = &attachment, .width = swapchainExtent_.width, .height = swapchainExtent_.height, .layers = 1};
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

void Renderer::recordCommandBuffer(VkCommandBuffer buffer, std::uint32_t imageIndex) {
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(buffer, &begin), "Beginning command buffer");
    const VkClearValue clear{{{0.015f, 0.02f, 0.05f, 1.0f}}};
    const VkRenderPassBeginInfo pass{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass_, .framebuffer = framebuffers_[imageIndex], .renderArea = {{0, 0}, swapchainExtent_}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{.width = static_cast<float>(swapchainExtent_.width), .height = static_cast<float>(swapchainExtent_.height), .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetViewport(buffer, 0, 1, &viewport); vkCmdSetScissor(buffer, 0, 1, &scissor);
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdDraw(buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(buffer);
    check(vkEndCommandBuffer(buffer), "Ending command buffer");
}

void Renderer::drawFrame() {
    vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);
    std::uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) check(acquire, "Acquiring swapchain image");
    check(vkResetFences(device_, 1, &inFlight_[currentFrame_]), "Resetting frame fence");
    check(vkResetCommandBuffer(commandBuffers_[currentFrame_], 0), "Resetting command buffer");
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);
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
    destroySwapchain(); createSwapchain(); createRenderPass(); createPipeline(); createFramebuffers();
}

void Renderer::destroySwapchain() {
    for (const auto framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    framebuffers_.clear();
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    pipeline_ = VK_NULL_HANDLE; pipelineLayout_ = VK_NULL_HANDLE; renderPass_ = VK_NULL_HANDLE;
    for (const auto view : swapchainImageViews_) vkDestroyImageView(device_, view, nullptr);
    swapchainImageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}
