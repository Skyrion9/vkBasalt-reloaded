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
        int32_t colorSpaceMode;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(ClarityRcasSpecData, field), .specSize = sizeof(((ClarityRcasSpecData*)0)->field)

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

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params = getParamDescs();
        ClarityRcasSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 1);

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

            if (p.type == ParamType::Float) {
                float f = (float)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &f, sizeof(float));
            } else {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        specData.colorSpaceMode = static_cast<int32_t>(csm);
        mapEntries.push_back({65535, offsetof(ClarityRcasSpecData, colorSpaceMode), sizeof(int32_t)});

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
            thirdBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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
            {.key = "clarityRStrength", .label = "Strength", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Master strength of the bilateral Clarity sharpening pass. Default 1.0.",
             SPEC(2, clarityStrength)},

            {.key = "clarityRBilateralRadius", .label = "Bilateral Radius", .type = ParamType::Float,
             .defaultVal = 2.0, .minVal = 0.5, .maxVal = 8.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Radius of the bilateral macro-contrast kernel. Larger = boosts wider features. Default 2.0.",
             SPEC(0, radius)},

            {.key = "clarityRBilateralOffset", .label = "Bilateral Offset", .type = ParamType::Float,
             .defaultVal = 1.5, .minVal = 0.5, .maxVal = 3.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Multiplier on the bilateral sample offset. Combined with Radius to determine fetch distance. Default 1.5.",
             SPEC(1, offset)},

            {.key = "clarityRBlendMode", .label = "Blend Mode", .type = ParamType::Int,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 6.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "How the sharpening delta blends with the original.\n"
                        "0: Soft Light\n1: Overlay (default)\n2: Hard Light\n3: Vivid Light (clamped)\n"
                        "4: Linear Light\n5: Additive\n6: Simple offset",
             SPEC(3, blendMode)},

            {.key = "clarityRBlendIfDark", .label = "Blend If Dark", .type = ParamType::Int,
             .defaultVal = 40.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "Pixels darker than this value receive reduced sharpening. 0 = sharpen everything. Default 40.",
             SPEC(4, blendIfDark)},

            {.key = "clarityRBlendIfLight", .label = "Blend If Light", .type = ParamType::Int,
             .defaultVal = 220.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "Pixels brighter than this value receive reduced sharpening. 255 = sharpen everything. Default 220.",
             SPEC(5, blendIfLight)},

            {.key = "clarityRcasSharpness", .label = "RCAS Sharpness", .type = ParamType::Float,
             .defaultVal = 0.8, .minVal = 0.0, .maxVal = 2.0, .step = 0.01,
             .category = "Sharpening",
             .tooltip = "AMD Robust Contrast-Adaptive Sharpening amount. Higher = more aggressive micro-detail sharpening. Default 0.8.",
             SPEC(6, rcasSharpness)},

            {.key = "clarityRcasStrength", .label = "RCAS Strength", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Master multiplier on the RCAS delta. Default 1.0.",
             SPEC(7, rcasStrength)},

            {.key = "clarityREdgeThreshLow", .label = "Edge Thresh Low", .type = ParamType::Float,
             .defaultVal = 0.05, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Protection",
             .tooltip = "Lower edge threshold. Contrast below this is suppressed. Lower = more fine detail. Default 0.05.",
             SPEC(8, edgeThreshLow)},

            {.key = "clarityREdgeThreshHigh", .label = "Edge Thresh High", .type = ParamType::Float,
             .defaultVal = 0.35, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Protection",
             .tooltip = "Upper edge threshold. Contrast above this passes through fully. Default 0.35.",
             SPEC(9, edgeThreshHigh)},

            {.key = "clarityREnableDithering", .label = "Enable Dithering", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Dithering",
             .tooltip = "Applies dithering to break up banding from the sharpening pass. Default on.",
             SPEC(10, enableDithering)},

            {.key = "clarityREnableFilmGrain", .label = "Enable Film Grain", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Film Grain",
             .tooltip = "Perceptual film grain with fine + coarse layers. Breaks up banding and adds natural texture. Default on.",
             SPEC(11, enableFilmGrain)},

            {.key = "clarityRFilmGrainStrength", .label = "Grain Strength", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
             .category = "Film Grain",
             .tooltip = "Master multiplier on grain amplitude. Default 1.0.",
             SPEC(12, filmGrainStrength)},

            {.key = "clarityRFilmGrainMinimum", .label = "Grain Minimum", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
             .category = "Film Grain",
             .tooltip = "Floor for grain intensity. Higher = grain everywhere. Default 0.0.",
             SPEC(13, filmGrainMinimum)},

            {.key = "clarityRFineGrainWeight", .label = "Fine Grain", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Film Grain",
             .tooltip = "Weight of the fine grain layer (1:1 pixel resolution, updates every frame). Default 0.4.",
             SPEC(14, fineGrainWeight)},

            {.key = "clarityRCoarseGrainWeight", .label = "Coarse Grain", .type = ParamType::Float,
             .defaultVal = 0.8, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Film Grain",
             .tooltip = "Weight of the coarse grain layer (1/4 resolution, updates every 2 frames). Default 0.8.",
             SPEC(15, coarseGrainWeight)},
        };
        return params;
    }
} // namespace vkBasalt
