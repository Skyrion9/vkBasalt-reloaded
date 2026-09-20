#include "effect_hdr_debug.hpp"

#include <cstring>

#include "shader_sources.hpp"
#include "logger.hpp"
#include "image_view.hpp"

namespace vkBasalt
{
    std::atomic<bool> g_hdrDebugToolActive{false};

    std::atomic<float> HdrDebugEffect::s_debugPeakNits{1000.0f};
    std::atomic<float> HdrDebugEffect::s_debugWhiteNits{203.0f};
    std::atomic<float> HdrDebugEffect::s_windowSize{0.10f};
    std::atomic<int> HdrDebugEffect::s_patternType{0};

    HdrDebugEffect::HdrDebugEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        const std::vector<VkImage>& outputImages,
        Config* /*pConfig*/) : m_dev(pLogicalDevice), m_extent(imageExtent), m_outImages(outputImages)
    {
        // Create image views for output images (Storage Images)
        m_outImageViews = createImageViews(pLogicalDevice, format, outputImages);

        // Create descriptor set layout for output storage image
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 0;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount                    = 1;
        layoutInfo.pBindings                       = &binding;
        m_dev->vkd.CreateDescriptorSetLayout(m_dev->device, &layoutInfo, nullptr, &m_setLayout);

        // Create descriptor pool
        VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = static_cast<uint32_t>(m_outImages.size())};
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets                    = static_cast<uint32_t>(m_outImages.size());
        poolInfo.poolSizeCount              = 1;
        poolInfo.pPoolSizes                 = &poolSize;
        m_dev->vkd.CreateDescriptorPool(m_dev->device, &poolInfo, nullptr, &m_descPool);

        // Allocate descriptor sets
        m_descSets.resize(m_outImages.size());
        std::vector<VkDescriptorSetLayout> layouts(m_outImages.size(), m_setLayout);
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool              = m_descPool;
        allocInfo.descriptorSetCount          = static_cast<uint32_t>(m_outImages.size());
        allocInfo.pSetLayouts                 = layouts.data();
        m_dev->vkd.AllocateDescriptorSets(m_dev->device, &allocInfo, m_descSets.data());

        // Update descriptor sets
        for (size_t i = 0; i < m_outImages.size(); i++) {
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.imageView             = m_outImageViews[i];
            imgInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet write = {};
            write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet               = m_descSets[i];
            write.dstBinding           = 0;
            write.descriptorCount      = 1;
            write.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo           = &imgInfo;
            m_dev->vkd.UpdateDescriptorSets(m_dev->device, 1, &write, 0, nullptr);
        }

        // Pipeline layout with push constants
        VkPushConstantRange pushRange = {};
        pushRange.stageFlags          = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset              = 0;
        pushRange.size                = sizeof(UboData);

        VkPipelineLayoutCreateInfo plInfo = {};
        plInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount             = 1;
        plInfo.pSetLayouts                = &m_setLayout;
        plInfo.pushConstantRangeCount     = 1;
        plInfo.pPushConstantRanges        = &pushRange;
        m_dev->vkd.CreatePipelineLayout(m_dev->device, &plInfo, nullptr, &m_pipelineLayout);

        // Create compute pipeline
        VkShaderModuleCreateInfo smInfo = {};
        smInfo.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        const auto& spirv               = decompressShaderCached(hdr_debug_pattern_comp);
        smInfo.codeSize                 = spirv.size() * sizeof(uint32_t);
        smInfo.pCode                    = spirv.data();

        VkShaderModule shaderModule = nullptr;
        m_dev->vkd.CreateShaderModule(m_dev->device, &smInfo, nullptr, &shaderModule);

        VkComputePipelineCreateInfo cpInfo = {};
        cpInfo.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpInfo.stage.sType                 = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module                = shaderModule;
        cpInfo.stage.pName                 = "main";
        cpInfo.layout                      = m_pipelineLayout;
        m_dev->vkd.CreateComputePipelines(m_dev->device, m_dev->pipelineCache, 1, &cpInfo, nullptr, &m_computePipeline);

        m_dev->vkd.DestroyShaderModule(m_dev->device, shaderModule, nullptr);

        Logger::debug("HdrDebugEffect (Compute) initialized");
    }

    HdrDebugEffect::~HdrDebugEffect()
    {
        if (!m_dev) return;
        if (m_computePipeline) m_dev->vkd.DestroyPipeline(m_dev->device, m_computePipeline, nullptr);
        if (m_pipelineLayout) m_dev->vkd.DestroyPipelineLayout(m_dev->device, m_pipelineLayout, nullptr);
        if (m_descPool) m_dev->vkd.DestroyDescriptorPool(m_dev->device, m_descPool, nullptr);
        if (m_setLayout) m_dev->vkd.DestroyDescriptorSetLayout(m_dev->device, m_setLayout, nullptr);

        for (auto view : m_outImageViews) {
            if (view) m_dev->vkd.DestroyImageView(m_dev->device, view, nullptr);
        }
    }

    void HdrDebugEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        // Barrier 1: Transition output image to GENERAL for compute write. Use UNDEFINED oldLayout to discard previous contents since we are overwriting the entire image.
        VkImageMemoryBarrier barrier = {};
        barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask        = 0;
        barrier.dstAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                = m_outImages[imageIndex];
        barrier.subresourceRange     = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        m_dev->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &barrier);

        // Bind pipeline and descriptor set
        m_dev->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
        m_dev->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descSets[imageIndex], 0, nullptr);

        // Push constants
        UboData pc = {
            .peakNits = s_debugPeakNits.load(), .windowSize = s_windowSize.load(), .patternType = s_patternType.load()};
        m_dev->vkd.CmdPushConstants(
            commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(UboData), &pc);

        // Dispatch
        uint32_t groupCountX = (m_extent.width + 15) / 16;
        uint32_t groupCountY = (m_extent.height + 15) / 16;
        m_dev->vkd.CmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

        // Barrier 2: Transition output image back to SHADER_READ_ONLY for the next effect (NitCalibration)
        VkImageMemoryBarrier barrier2 = {};
        barrier2.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier2.srcAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
        barrier2.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        barrier2.oldLayout            = VK_IMAGE_LAYOUT_GENERAL;
        barrier2.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image                = m_outImages[imageIndex];
        barrier2.subresourceRange     = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        m_dev->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
            0, nullptr, 1, &barrier2);
    }

} // namespace vkBasalt
