#include "Renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void checkShadow(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}
VkShaderModule shadowShader(VkDevice device, const char* name) {
    const char* base = SDL_GetBasePath();
    const auto directory = base ? std::filesystem::path(base) : std::filesystem::current_path();
    const auto path = directory / "shaders" / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Could not open shadow shader: " + path.string());
    const auto bytes = static_cast<std::size_t>(file.tellg());
    if (!bytes || bytes % sizeof(std::uint32_t)) throw std::runtime_error("Invalid shadow SPIR-V");
    std::vector<std::uint32_t> code(bytes / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes, .pCode = code.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    checkShadow(vkCreateShaderModule(device, &info, nullptr, &module), "Creating shadow shader");
    return module;
}
}

void Renderer::createShadowResources() {
    VkFormat format = VK_FORMAT_UNDEFINED;
    for (const auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, candidate, &properties);
        const auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) { format = candidate; break; }
    }
    if (format == VK_FORMAT_UNDEFINED) throw std::runtime_error("No sampleable shadow depth format");
    const VkAttachmentDescription depth{.format = format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pDepthStencilAttachment = &reference};
    const auto depthStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const std::array<VkSubpassDependency, 2> dependencies{{
        {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .dstStageMask = depthStages,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = depthStages, .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT}}};
    const VkRenderPassCreateInfo pass{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &depth, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = static_cast<std::uint32_t>(dependencies.size()), .pDependencies = dependencies.data()};
    checkShadow(vkCreateRenderPass(device_, &pass, nullptr, &shadowRenderPass_), "Creating shadow render pass");
    for (auto& frame : shadowFrames_) {
        frame.uniforms = createUniformBuffer(sizeof(ShadowUniforms));
        frame.map = createImage(shadowMapResolution, shadowMapResolution, 1, format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowCascadeCount);
        VkImageViewCreateInfo view{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = frame.map.image, .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, shadowCascadeCount}};
        checkShadow(vkCreateImageView(device_, &view, nullptr, &frame.map.imageView), "Creating shadow array view");
        const VkSamplerCreateInfo sampler{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .compareEnable = VK_TRUE, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
        // PCF weights and depth comparisons are combined in shadow_sampling.glsl.
        checkShadow(vkCreateSampler(device_, &sampler, nullptr, &frame.map.sampler), "Creating shadow comparison sampler");
        for (std::uint32_t layer = 0; layer < shadowCascadeCount; ++layer) {
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.subresourceRange.baseArrayLayer = layer;
            view.subresourceRange.layerCount = 1;
            checkShadow(vkCreateImageView(device_, &view, nullptr, &frame.layerViews[layer]), "Creating cascade view");
            const VkFramebufferCreateInfo framebuffer{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = shadowRenderPass_, .attachmentCount = 1, .pAttachments = &frame.layerViews[layer],
                .width = shadowMapResolution, .height = shadowMapResolution, .layers = 1};
            checkShadow(vkCreateFramebuffer(device_, &framebuffer, nullptr, &frame.framebuffers[layer]), "Creating cascade framebuffer");
        }
    }
    const std::array<VkDescriptorSetLayout, 2> sets{frameDescriptorSetLayout_, materialDescriptorSetLayout_};
    const VkPushConstantRange object{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowObjectUniforms)};
    const VkPipelineLayoutCreateInfo layout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(sets.size()), .pSetLayouts = sets.data(),
        .pushConstantRangeCount = 1, .pPushConstantRanges = &object};
    checkShadow(vkCreatePipelineLayout(device_, &layout, nullptr, &shadowPipelineLayout_), "Creating shadow pipeline layout");
    const auto vertex = shadowShader(device_, "shadow.vert.spv");
    const auto fragment = shadowShader(device_, "shadow.frag.spv");
    const VkPipelineShaderStageCreateInfo stages[]{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, baseColorUv)},
        {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}}};
    const VkPipelineVertexInputStateCreateInfo input{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()), .pVertexAttributeDescriptions = attributes.data()};
    VkPipelineInputAssemblyStateCreateInfo assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .depthBiasEnable = VK_TRUE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo samples{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineDepthStencilStateCreateInfo depthTest{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};
    const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 3, .pDynamicStates = states};
    const VkGraphicsPipelineCreateInfo pipeline{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport, .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pDepthStencilState = &depthTest, .pColorBlendState = &blend, .pDynamicState = &dynamic,
        .layout = shadowPipelineLayout_, .renderPass = shadowRenderPass_};
    constexpr VkPrimitiveTopology topologies[]{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN};
    VkResult result = VK_SUCCESS;
    for (std::size_t i = 0; i < shadowPipelines_.size(); ++i) {
        assembly.topology = topologies[i / 4];
        raster.frontFace = i & 1 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.cullMode = i & 2 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &shadowPipelines_[i]);
        if (result != VK_SUCCESS) break;
    }
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    checkShadow(result, "Creating shadow pipelines");
}

void Renderer::destroyShadowResources() {
    for (auto pipeline : shadowPipelines_) if (pipeline) vkDestroyPipeline(device_, pipeline, nullptr);
    shadowPipelines_.fill(VK_NULL_HANDLE);
    if (shadowPipelineLayout_) vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
    shadowPipelineLayout_ = VK_NULL_HANDLE;
    for (auto& frame : shadowFrames_) {
        for (auto framebuffer : frame.framebuffers) if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
        for (auto view : frame.layerViews) if (view) vkDestroyImageView(device_, view, nullptr);
        destroyImage(frame.map);
        destroyBuffer(frame.uniforms);
        frame = {};
    }
    if (shadowRenderPass_) vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
    shadowRenderPass_ = VK_NULL_HANDLE;
}

void Renderer::updateShadows(const Scene& scene, const Camera& camera, const RenderSettings& settings) {
    auto& frame = shadowFrames_[currentFrame_];
    frame.enabled = settings.shadows && settings.worldLightEnabled && settings.worldLightIntensity > 0 &&
        glm::length(settings.worldLightDirection) > 1e-6f && settings.shadowDistance > camera.nearPlane;
    ShadowUniforms uniforms{};
    const auto view = camera.view();
    std::copy_n(glm::value_ptr(view), 16, uniforms.view.begin());
    uniforms.options = {frame.enabled ? 1.0f : 0.0f, std::clamp(settings.shadowStrength, 0.0f, 1.0f),
        std::max(settings.shadowBias, 0.0f), settings.showShadowCascades ? 1.0f : 0.0f};
    uniforms.alphaOptions = {settings.baseColorTextures ? 1.0f : 0.0f, settings.vertexColors ? 1.0f : 0.0f, 0, 0};
    if (frame.enabled) {
        const auto cascades = buildShadowCascades(camera, static_cast<float>(swapchainExtent_.width) / swapchainExtent_.height,
            settings.worldLightDirection, settings.shadowDistance, scene.worldBounds());
        for (std::uint32_t i = 0; i < shadowCascadeCount; ++i) {
            std::copy_n(glm::value_ptr(cascades.lightViewProjection[i]), 16, uniforms.lightViewProjection[i].begin());
            uniforms.splitDepths[i] = cascades.splitDepths[i];
        }
        uniforms.splitDepths[3] = camera.nearPlane;
    }
    updateUniformBuffer(frame.uniforms, &uniforms, sizeof(uniforms));
}

void Renderer::recordShadows(VkCommandBuffer buffer, const Scene& scene, const RenderSettings& settings) {
    auto& frame = shadowFrames_[currentFrame_];
    if (!frame.enabled && frame.initialized) return;
    const VkViewport viewport{.width = static_cast<float>(shadowMapResolution),
        .height = static_cast<float>(shadowMapResolution), .minDepth = 0, .maxDepth = 1};
    const VkRect2D scissor{{0,0}, {shadowMapResolution, shadowMapResolution}};
    const VkClearValue clear{.depthStencil = {1,0}};
    for (std::uint32_t cascade = 0; cascade < shadowCascadeCount; ++cascade) {
        const VkRenderPassBeginInfo pass{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = shadowRenderPass_, .framebuffer = frame.framebuffers[cascade],
            .renderArea = scissor, .clearValueCount = 1, .pClearValues = &clear};
        vkCmdBeginRenderPass(buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(buffer, 0, 1, &viewport);
        vkCmdSetScissor(buffer, 0, 1, &scissor);
        vkCmdSetDepthBias(buffer, 1.25f, 0, std::max(settings.shadowSlopeBias, 0.0f));
        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 0, 1,
            &frameDescriptorSets_[currentFrame_], 0, nullptr);
        if (frame.enabled) for (const auto& node : scene.nodes) {
            if (node.guide) continue;
            const float determinant = glm::determinant(glm::mat3(node.world));
            if (!std::isfinite(determinant) || determinant == 0) continue;
            ShadowObjectUniforms object{};
            std::copy_n(glm::value_ptr(node.world), 16, object.object.model.begin());
            object.cascade = cascade;
            vkCmdPushConstants(buffer, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(object), &object);
            // Submit all potential casters, including those outside the camera frustum.
            // Light-space clipping restricts rasterization to this cascade's volume.
            for (const auto index : node.primitives) {
                const auto& primitive = scene.primitives.at(index);
                const auto& material = scene.materials.at(primitive.material);
                if (material.alphaMode == AlphaMode::Blend) continue;
                const auto& mesh = meshes_.at(index);
                const std::size_t group = mesh.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ? 1 :
                    mesh.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN ? 2 : 0;
                const auto pipeline = group * 4 + (determinant < 0 ? 1 : 0) + (material.doubleSided ? 2 : 0);
                vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelines_[pipeline]);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(buffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
                vkCmdBindIndexBuffer(buffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 1, 1,
                    &materials_.at(primitive.material).descriptorSet, 0, nullptr);
                vkCmdDrawIndexed(buffer, mesh.indexCount, 1, 0, 0, 0);
            }
        }
        vkCmdEndRenderPass(buffer);
    }
    frame.initialized = true;
}
