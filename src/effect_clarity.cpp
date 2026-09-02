#include "effect_clarity.hpp"

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
#include "logger.hpp"
#include "logical_device.hpp"
#include "util.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    #define SPEC(id, field) .specId = id, .specOffset = offsetof(ClaritySpecData, field), .specSize = sizeof(((ClaritySpecData*)0)->field)
    ClarityEffect::ClarityEffect(LogicalDevice*       pLogicalDevice,
                                 VkFormat             format,
                                 VkExtent2D           imageExtent,
                                 std::vector<VkImage> inputImages,
                                 std::vector<VkImage> outputImages,
                                 Config*              pConfig,
                                 VkColorSpaceKHR      colorSpace)
    {
        Logger::debug("in creating ClarityEffect");
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarity_frag;
        this->pushConstantSize = sizeof(ClarityPushConstants);

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params = getParamDescs();
        ClaritySpecData specData = {};
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
        mapEntries.push_back({65535, offsetof(ClaritySpecData, colorSpaceMode), sizeof(int32_t)});

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
        specializationInfo.dataSize      = sizeof(ClaritySpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    ClarityEffect::~ClarityEffect()
    {
        // Base class SimpleEffect::~SimpleEffect() handles all the Vulkan resource cleanup.
    }

    void ClarityEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
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
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdPushConstants(
            commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ClarityPushConstants), &pushConstants);
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
            thirdBarrier.oldLayout           = finalLayout;
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

    const std::vector<EffectParamDesc>& ClarityEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "clarityStrength", .label = "Strength", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Master strength of the bilateral sharpening pass. Controls how strongly the macro-contrast delta is applied. Default 1.0.",
             SPEC(2, strength)},

            {.key = "clarityRadius", .label = "Radius", .type = ParamType::Float,
             .defaultVal = 2.0, .minVal = 1.0, .maxVal = 8.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "Radius of the bilateral contrast kernel. Larger = boosts wider features. Smaller = fine detail only. Default 2.0.",
             SPEC(0, radius)},

            {.key = "clarityOffset", .label = "Offset", .type = ParamType::Float,
             .defaultVal = 1.5, .minVal = 0.5, .maxVal = 3.0, .step = 0.1,
             .category = "Sharpening",
             .tooltip = "Multiplier on the bilateral sample offset. Combined with Radius to determine fetch distance. Default 1.5.",
             SPEC(1, offset)},

            {.key = "clarityBlendMode", .label = "Blend Mode", .type = ParamType::Int,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 6.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "How the sharpening delta blends with the original.\n"
                        "0: Soft Light\n1: Overlay (default)\n2: Hard Light\n3: Vivid Light (clamped)\n"
                        "4: Linear Light\n5: Additive\n6: Simple offset",
             SPEC(3, blendMode)},

            {.key = "clarityBlendIfDark", .label = "Blend If Dark", .type = ParamType::Int,
             .defaultVal = 40.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "Pixels darker than this value receive reduced sharpening. 0 = sharpen everything. Default 40.",
             SPEC(4, blendIfDark)},

            {.key = "clarityBlendIfLight", .label = "Blend If Light", .type = ParamType::Int,
             .defaultVal = 220.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening",
             .tooltip = "Pixels brighter than this value receive reduced sharpening. 255 = sharpen everything. Default 220.",
             SPEC(5, blendIfLight)},

            {.key = "clarityEdgeThreshLow", .label = "Edge Thresh Low", .type = ParamType::Float,
             .defaultVal = 0.05, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Protection",
             .tooltip = "Lower edge threshold. Contrast differences below this are fully suppressed. Lower = more fine detail passes through. Default 0.05.",
             SPEC(6, edgeThreshLow)},

            {.key = "clarityEdgeThreshHigh", .label = "Edge Thresh High", .type = ParamType::Float,
             .defaultVal = 0.25, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Protection",
             .tooltip = "Upper edge threshold. Contrast differences above this are fully passed through. The Low-to-High range is the smooth transition zone. Default 0.25.",
             SPEC(7, edgeThreshHigh)},

            {.key = "clarityEnableDithering", .label = "Enable Dithering", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Dithering",
             .tooltip = "Applies dithering after sharpening to break up banding introduced by the contrast boost. Default on.",
             SPEC(8, enableDithering)},
        };
        return params;
    }
} // namespace vkBasalt
