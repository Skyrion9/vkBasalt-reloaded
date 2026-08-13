#include "effect_clarity.hpp"

#include <array>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <algorithm>
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

        auto getAndStore = [&](const std::string& key, double defaultVal) -> double {
            double val = pConfig->getOption<float>(key, (float)defaultVal);
            m_paramValues[key] = val;
            return val;
        };
        auto getAndStoreInt = [&](const std::string& key, double defaultVal) -> double {
            double val = (double)pConfig->getOption<int32_t>(key, (int32_t)defaultVal);
            m_paramValues[key] = val;
            return val;
        };

        this->radius = std::clamp((float)getAndStore("clarityRadius", 2.0f), 1.0f, 8.0f);
        this->offset = std::clamp((float)getAndStore("clarityOffset", 1.5f), 0.5f, 3.0f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);
        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

        struct ClaritySpecData {
            float radius; float offset; float strength; int32_t blendMode;
            int32_t blendIfDark; int32_t blendIfLight; float edgeThreshLow;
            float edgeThreshHigh; int32_t enableDithering; int32_t hdrMode;
        };

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        ClaritySpecData specData;
        specData.radius          = this->radius;
        specData.offset          = this->offset;
        specData.strength        = std::clamp((float)getAndStore("clarityStrength", 1.0f), 0.0f, 5.0f);
        specData.blendMode       = std::clamp((int32_t)getAndStoreInt("clarityBlendMode", 1), 0, 6);
        specData.blendIfDark     = std::clamp((int32_t)getAndStoreInt("clarityBlendIfDark", 40), 0, 255);
        specData.blendIfLight    = std::clamp((int32_t)getAndStoreInt("clarityBlendIfLight", 220), 0, 255);
        specData.edgeThreshLow   = std::clamp((float)getAndStore("clarityEdgeThreshLow", 0.05f), 0.0f, 1.0f);
        specData.edgeThreshHigh  = std::clamp((float)getAndStore("clarityEdgeThreshHigh", 0.25f), 0.0f, 1.0f);
        specData.enableDithering = std::clamp((int32_t)getAndStoreInt("clarityEnableDithering", 1), 0, 1);
        specData.hdrMode         = isHDR ? 1 : 0;

        std::array<VkSpecializationMapEntry, 10> mapEntries = {{
            {0, offsetof(ClaritySpecData, radius),         sizeof(float)},
            {1, offsetof(ClaritySpecData, offset),         sizeof(float)},
            {2, offsetof(ClaritySpecData, strength),       sizeof(float)},
            {3, offsetof(ClaritySpecData, blendMode),      sizeof(int32_t)},
            {4, offsetof(ClaritySpecData, blendIfDark),    sizeof(int32_t)},
            {5, offsetof(ClaritySpecData, blendIfLight),   sizeof(int32_t)},
            {6, offsetof(ClaritySpecData, edgeThreshLow),  sizeof(float)},
            {7, offsetof(ClaritySpecData, edgeThreshHigh), sizeof(float)},
            {8, offsetof(ClaritySpecData, enableDithering),sizeof(int32_t)},
            {9, offsetof(ClaritySpecData, hdrMode),        sizeof(int32_t)}
        }};

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = mapEntries.size();
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
        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext               = nullptr;
        memoryBarrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT; 
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkImageMemoryBarrier secondBarrier;
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.pNext               = nullptr;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext             = nullptr;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        pLogicalDevice->vkd.CmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ClarityPushConstants),
            &pushConstants
        );

        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);

        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                                               0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
    }

    const std::vector<EffectParamDesc>& ClarityEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"clarityStrength",      "Strength",        ParamType::Float, 1.0,   0.0,   5.0,   0.1},
            {"clarityRadius",        "Radius",          ParamType::Float, 2.0,   1.0,   8.0,   1.0},
            {"clarityOffset",        "Offset",          ParamType::Float, 1.5,   0.5,   3.0,   0.1},
            {"clarityBlendMode",     "Blend Mode",      ParamType::Int,   1.0,   0.0,   6.0,   1.0},
            {"clarityBlendIfDark",   "Blend If Dark",   ParamType::Int,  40.0,   0.0, 255.0,   1.0},
            {"clarityBlendIfLight",  "Blend If Light",  ParamType::Int, 220.0,   0.0, 255.0,   1.0},
            {"clarityEdgeThreshLow", "Edge Thresh Low", ParamType::Float, 0.05,  0.0,   1.0,  0.01},
            {"clarityEdgeThreshHigh","Edge Thresh High",ParamType::Float, 0.25,  0.0,   1.0,  0.01},
            {"clarityEnableDithering","Enable Dithering",ParamType::Bool, 1.0,   0.0,   1.0,   1.0},
        };
        return params;
    }

} // namespace vkBasalt
