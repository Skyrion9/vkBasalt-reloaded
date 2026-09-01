#include "effect_nit_calibration.hpp"
#include "shader_sources.hpp"
#include "logger.hpp"
#include <vulkan/vulkan_core.h>

namespace vkBasalt {

    NitCalibrationEffect::NitCalibrationEffect(LogicalDevice* pLogicalDevice, 
                                               VkFormat sourceFormat, VkFormat destFormat, VkExtent2D imageExtent,
                                               std::vector<VkImage> inputImages, std::vector<VkImage> outputImages,
                                               Config* pConfig, 
                                               VkColorSpaceKHR sourceColorSpace, VkColorSpaceKHR destColorSpace,
                                               bool autoHdrActive) {
        Logger::debug("Creating HDR Output Effect");
        vertexCode = full_screen_triangle_vert;
        fragmentCode = nit_calibration_frag;
        
        m_pConfigRef = pConfig;
        m_autoHdrActive = autoHdrActive;
        
        std::string toneMapperMode = pConfig->getOption<std::string>("hdrToneMapper", "quality");
        m_toneMapperModeInt = (toneMapperMode == "fast" || toneMapperMode == "igpu") ? 1 : 0;
        
        m_sourceColorSpaceInt = static_cast<int32_t>(getColorSpaceMode(sourceFormat, sourceColorSpace));
        m_destColorSpaceInt = static_cast<int32_t>(getColorSpaceMode(destFormat, destColorSpace));
        
        m_specData = {
            pConfig->getOption<float>("sdrWhitePointNits", 203.0f),
            pConfig->getOption<float>("hdrPeakNits", 1000.0f),
            m_autoHdrActive ? 1 : 0,
            m_toneMapperModeInt,
            m_sourceColorSpaceInt,
            m_destColorSpaceInt
        };

        m_specMapEntries[0] = { 0, offsetof(NitCalibrationSpecData, sdrWhitePoint), sizeof(float) };
        m_specMapEntries[1] = { 1, offsetof(NitCalibrationSpecData, hdrPeakNits), sizeof(float) };
        m_specMapEntries[2] = { 2, offsetof(NitCalibrationSpecData, autoHdrEnabled), sizeof(int32_t) };
        m_specMapEntries[3] = { 65533, offsetof(NitCalibrationSpecData, toneMapperMode), sizeof(int32_t) };
        m_specMapEntries[4] = { 65534, offsetof(NitCalibrationSpecData, sourceColorSpace), sizeof(int32_t) };
        m_specMapEntries[5] = { 65535, offsetof(NitCalibrationSpecData, destColorSpace), sizeof(int32_t) };
        
        m_specInfo.mapEntryCount = 6;
        m_specInfo.pMapEntries = m_specMapEntries;
        m_specInfo.dataSize = sizeof(NitCalibrationSpecData);
        m_specInfo.pData = &m_specData;
        
        pVertexSpecInfo = nullptr;
        pFragmentSpecInfo = &m_specInfo;
        
        // Init with destFormat so renderpass/framebuffers match the real HDR swapchain
        init(pLogicalDevice, destFormat, imageExtent, inputImages, outputImages, pConfig);

        // Recreate input views with sourceFormat as fake images are SDR
        if (sourceFormat != destFormat) {
            for (size_t i = 0; i < inputImages.size(); i++) {
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
                VkImageViewCreateInfo viewInfo = {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = inputImages[i];
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = sourceFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &viewInfo, nullptr, &inputImageViews[i]);

                VkDescriptorImageInfo imgInfo = {};
                imgInfo.sampler = sampler;
                imgInfo.imageView = inputImageViews[i];
                imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = imageDescriptorSets[i];
                write.dstBinding = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imgInfo;
                pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &write, 0, nullptr);
            }
        }
    }

    NitCalibrationEffect::~NitCalibrationEffect() {}

    void NitCalibrationEffect::updateEffect() {}

    void NitCalibrationEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) {
        // Barrier 1: Acquire inputImages for reading
        VkImageMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        // Render Pass (writes to outputImages, automatically transitions them to finalLayout = PRESENT_SRC_KHR)
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier secondBarrier = {};
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &secondBarrier);

        // Barrier 3: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier thirdBarrier = {};
            thirdBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            thirdBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Left by render pass
            thirdBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            thirdBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.image               = outputImages[imageIndex];
            thirdBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &thirdBarrier);
        }
    }

} // namespace vkBasalt
