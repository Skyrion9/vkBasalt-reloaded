#include "effect_nit_calibration.hpp"
#include "shader_sources.hpp"
#include "logger.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>

namespace vkBasalt 
{
    #define SPEC(id, field) .specId = id, .specOffset = offsetof(NitCalibrationSpecData, field), .specSize = sizeof(((NitCalibrationSpecData*)0)->field)

    // Static accessor, callable without an instance (used by AutoHDR tab when effect isn't in chain)
    const std::vector<EffectParamDesc>& NitCalibrationEffect::getCalibrationParams()
    {
        static const std::vector<EffectParamDesc> params = {
            {.key = "hdrToneMapper", .label = "Tone Mapper", .type = ParamType::Combo,
             .defaultVal = 2.0, .minVal = 0.0, .maxVal = 2.0, .step = 1.0,
             .comboOptions = {"quality", "fast", "hermite"},
             .category = "Display Calibration",
             .tooltip = "HDR tone mapping algorithm.\n"
                        "quality: Reinhard, rational curve with matched slope, Hunt effect, achromatic clipping.\n"
                        "fast: Polynomial sRGB decode, simpler curve, MaxRGB clamp.\n"
                        "hermite: BT.2390 cubic spline, C1 continuous roll-off (default).",
             SPEC(65533, toneMapperMode)},

            {.key = "sdrWhitePointNits", .label = "SDR White Point (nits)", .type = ParamType::Float,
             .defaultVal = 203.0, .minVal = 80.0, .maxVal = 400.0, .step = 1.0,
             .category = "Display Calibration",
             .tooltip = "Reference white luminance for SDR content.\n"
                        "100 = ITU-R BT.709 reference\n"
                        "203 = HDR10 standard SDR white\n"
                        "80-120 = dim room viewing",
             SPEC(0, sdrWhitePoint)},

            {.key = "hdrPeakNits", .label = "Peak Brightness (nits)", .type = ParamType::Float,
             .defaultVal = 1000.0, .minVal = 200.0, .maxVal = 4000.0, .step = 10.0,
             .category = "Display Calibration",
             .tooltip = "Maximum display luminance.\n"
                        "Clamps HDR highlights to your display's measured peak.\n"
                        "Common values: 400 (entry HDR), 600-1000 (mid-range), 1000-2000 (high-end OLED/MiniLED).",
             SPEC(1, hdrPeakNits)},
        };
        return params;
    }

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
        
        // Read config, apply defaults, clamp, write to specData by offset, and build mapEntries
        NitCalibrationSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(getParamDescs().size() + 3); // +3 for autoHdr, source/dest colorspace

        const auto& params = getParamDescs();
        for (const auto& p : params) {
            if (p.specId < 0) continue;
            
            double def = p.defaultVal;
            double val;
            
            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = static_cast<int>(p.defaultVal);
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) {
                        idx = static_cast<int>(ci);
                        break;
                    }
                }
                val = static_cast<double>(idx);
            } else if (p.type == ParamType::Float) {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(def)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(def)));
            }
            
            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;
            
            if (p.type == ParamType::Float) {
                float f = (float)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &f, sizeof(float));
            } else {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &i, sizeof(int32_t));
            }
            
            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        // Add non-user-configurable specialization constants
        specData.autoHdrEnabled = autoHdrActive ? 1 : 0;
        mapEntries.push_back({2, offsetof(NitCalibrationSpecData, autoHdrEnabled), sizeof(int32_t)});
        
        specData.sourceColorSpace = static_cast<int32_t>(getColorSpaceMode(sourceFormat, sourceColorSpace));
        mapEntries.push_back({65534, offsetof(NitCalibrationSpecData, sourceColorSpace), sizeof(int32_t)});
        
        specData.destColorSpace = static_cast<int32_t>(getColorSpaceMode(destFormat, destColorSpace));
        mapEntries.push_back({65535, offsetof(NitCalibrationSpecData, destColorSpace), sizeof(int32_t)});
        
        m_specData = specData;
        m_specMapEntries = mapEntries;
        
        m_specInfo.mapEntryCount = (uint32_t)m_specMapEntries.size();
        m_specInfo.pMapEntries = m_specMapEntries.data();
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
