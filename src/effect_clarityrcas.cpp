#include "effect_clarityrcas.hpp"

#include <cstdint>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "effect.hpp"
#include "effect_simple.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "util.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{

    struct ClarityRcasSpecData {
        float radius;
        float offset;
        float clarityStrength;
        int32_t blendMode;
        int32_t blendIfDark;
        int32_t blendIfLight;
        float rcasSharpness;
        float rcasStrength;
        float edgeThreshLow;
        float edgeThreshHigh;
        int32_t enableDithering;
        int32_t enableFilmGrain;
        float filmGrainStrength;
        float filmGrainMinimum;
        float fineGrainWeight;
        float coarseGrainWeight;
        int32_t hdrMode;
    };

    #define SPEC(id, field) id, offsetof(ClarityRcasSpecData, field), sizeof(((ClarityRcasSpecData*)0)->field)

    ClarityRcasEffect::ClarityRcasEffect(LogicalDevice*       pLogicalDevice,
                                         VkFormat             format,
                                         VkExtent2D           imageExtent,
                                         std::vector<VkImage> inputImages,
                                         std::vector<VkImage> outputImages,
                                         Config*              pConfig,
                                         VkColorSpaceKHR      colorSpace)
    {
        Logger::debug("in creating ClarityRcasEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarityrcas_frag;
        this->pushConstantSize = sizeof(ClarityRcasPushConstants);
        needsUniformBuffer = true;
        uniformSize = sizeof(FrameData);

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        const auto& params = getParamDescs();
        ClarityRcasSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val;
            if (p.type == ParamType::Float) {
                val = (double)pConfig->getOption<float>(p.key, (float)p.defaultVal);
            } else {
                val = (double)pConfig->getOption<int32_t>(p.key, (int32_t)p.defaultVal);
            }

            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.specSize == sizeof(float)) {
                float f = (float)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &f, sizeof(float));
            } else if (p.specSize == sizeof(int32_t)) {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        specData.hdrMode = isHDR ? 1 : 0;
        mapEntries.push_back({16, offsetof(ClarityRcasSpecData, hdrMode), sizeof(int32_t)});

        this->radius = specData.radius;
        this->offset = specData.offset;
        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);
        float rawOffset  = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;
        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(ClarityRcasSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    ClarityRcasEffect::~ClarityRcasEffect()
    {
        // Base class SimpleEffect::~SimpleEffect() handles UBO cleanup
    }

    void ClarityRcasEffect::updateEffect()
    {
        // Write the new frame counter directly to the mapped GPU memory.
        if (mappedUniform) {
            FrameData* data = static_cast<FrameData*>(mappedUniform);
            data->frameCounter = m_frameCounter++;
        }
    }

    void ClarityRcasEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) {
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

        // Render Pass (writes to outputImages, automatically transitions them to finalLayout)
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ClarityRcasPushConstants), &pushConstants);
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

    const std::vector<EffectParamDesc>& ClarityRcasEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"clarityRStrength",          "Strength",          ParamType::Float, 1.0,   0.0,   5.0,   0.1,  {}, "Sharpening",     SPEC(2, clarityStrength)},
            {"clarityRBilateralRadius",   "Bilateral Radius",  ParamType::Float, 2.0,   0.5,   8.0,   0.1,  {}, "Sharpening",     SPEC(0, radius)},
            {"clarityRBilateralOffset",   "Bilateral Offset",  ParamType::Float, 1.5,   0.5,   3.0,   0.1,  {}, "Sharpening",     SPEC(1, offset)},
            {"clarityRBlendMode",         "Blend Mode",        ParamType::Int,   1.0,   0.0,   6.0,   1.0,  {}, "Sharpening",     SPEC(3, blendMode)},
            {"clarityRBlendIfDark",       "Blend If Dark",     ParamType::Int,  40.0,   0.0, 255.0,   1.0,  {}, "Sharpening",     SPEC(4, blendIfDark)},
            {"clarityRBlendIfLight",      "Blend If Light",    ParamType::Int, 220.0,   0.0, 255.0,   1.0,  {}, "Sharpening",     SPEC(5, blendIfLight)},
            {"clarityRcasSharpness",      "RCAS Sharpness",    ParamType::Float, 0.8,   0.0,   2.0,  0.01,  {}, "Sharpening",     SPEC(6, rcasSharpness)},
            {"clarityRcasStrength",       "RCAS Strength",     ParamType::Float, 1.0,   0.0,   5.0,   0.1,  {}, "Sharpening",     SPEC(7, rcasStrength)},
            {"clarityREdgeThreshLow",     "Edge Thresh Low",   ParamType::Float, 0.05,  0.0,   1.0,  0.01,  {}, "Protection",     SPEC(8, edgeThreshLow)},
            {"clarityREdgeThreshHigh",    "Edge Thresh High",  ParamType::Float, 0.35,  0.0,   1.0,  0.01,  {}, "Protection",     SPEC(9, edgeThreshHigh)},
            {"clarityREnableDithering",   "Enable Dithering",  ParamType::Bool,  1.0,   0.0,   1.0,   1.0,  {}, "Dithering",      SPEC(10, enableDithering)},
            {"clarityREnableFilmGrain",   "Enable Film Grain", ParamType::Bool,  1.0,   0.0,   1.0,   1.0,  {}, "Film Grain",     SPEC(11, enableFilmGrain)},
            {"clarityRFilmGrainStrength", "Grain Strength",    ParamType::Float, 1.0,   0.0,   2.0,   0.1,  {}, "Film Grain",     SPEC(12, filmGrainStrength)},
            {"clarityRFilmGrainMinimum",  "Grain Minimum",     ParamType::Float, 0.0,   0.0,   2.0,   0.1,  {}, "Film Grain",     SPEC(13, filmGrainMinimum)},
            {"clarityRFineGrainWeight",   "Fine Grain",        ParamType::Float, 0.4,   0.0,   1.0,  0.01,  {}, "Film Grain",     SPEC(14, fineGrainWeight)},
            {"clarityRCoarseGrainWeight", "Coarse Grain",      ParamType::Float, 0.8,   0.0,   1.0,  0.01,  {}, "Film Grain",     SPEC(15, coarseGrainWeight)},
        };
        return params;
    }
} // namespace vkBasalt
