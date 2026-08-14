#include "effect_clarityrcas.hpp"

#include <cstdint>
#include <cmath>
#include <cstddef> 
#include <algorithm>
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

        this->radius = std::clamp((float)getAndStore("clarityRBilateralRadius", 2.0f), 0.5f, 8.0f);
        this->offset = std::clamp((float)getAndStore("clarityRBilateralOffset", 1.5f), 0.5f, 3.0f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);
        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

        struct ClarityRcasSpecData {
            float radius; float offset; float clarityStrength; int32_t blendMode;
            int32_t blendIfDark; int32_t blendIfLight; float rcasSharpness; float rcasStrength;
            float edgeThreshLow; float edgeThreshHigh; int32_t enableDithering; int32_t enableFilmGrain;
            float filmGrainStrength; float filmGrainMinimum; float fineGrainWeight; float coarseGrainWeight;
            int32_t hdrMode;
        };

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        ClarityRcasSpecData specData;
        specData.radius               = this->radius;
        specData.offset               = this->offset;
        specData.clarityStrength      = std::clamp((float)getAndStore("clarityRStrength", 1.0f), 0.0f, 5.0f);
        specData.blendMode            = std::clamp((int32_t)getAndStoreInt("clarityRBlendMode", 1), 0, 6);
        specData.blendIfDark          = std::clamp((int32_t)getAndStoreInt("clarityRBlendIfDark", 40), 0, 255);
        specData.blendIfLight         = std::clamp((int32_t)getAndStoreInt("clarityRBlendIfLight", 220), 0, 255);
        specData.rcasSharpness        = std::clamp((float)getAndStore("clarityRcasSharpness", 0.8f), 0.0f, 2.0f);
        specData.rcasStrength         = std::clamp((float)getAndStore("clarityRcasStrength", 1.0f), 0.0f, 5.0f);
        specData.edgeThreshLow        = std::clamp((float)getAndStore("clarityREdgeThreshLow", 0.05f), 0.0f, 1.0f);
        specData.edgeThreshHigh       = std::clamp((float)getAndStore("clarityREdgeThreshHigh", 0.35f), 0.0f, 1.0f);
        specData.enableDithering      = std::clamp((int32_t)getAndStoreInt("clarityREnableDithering", 1), 0, 1);
        specData.enableFilmGrain      = std::clamp((int32_t)getAndStoreInt("clarityREnableFilmGrain", 1), 0, 1);
        specData.filmGrainStrength    = std::clamp((float)getAndStore("clarityRFilmGrainStrength", 1.0f), 0.0f, 2.0f);
        specData.filmGrainMinimum     = std::clamp((float)getAndStore("clarityRFilmGrainMinimum", 0.0f), 0.0f, 2.0f);
        specData.fineGrainWeight      = std::clamp((float)getAndStore("clarityRFineGrainWeight", 0.4f), 0.0f, 1.0f);
        specData.coarseGrainWeight    = std::clamp((float)getAndStore("clarityRCoarseGrainWeight", 0.8f), 0.0f, 1.0f);
        specData.hdrMode              = isHDR ? 1 : 0;

        VkSpecializationMapEntry mapEntries[] = {
            {0,  offsetof(ClarityRcasSpecData, radius),                sizeof(float)},
            {1,  offsetof(ClarityRcasSpecData, offset),                sizeof(float)},
            {2,  offsetof(ClarityRcasSpecData, clarityStrength),       sizeof(float)},
            {3,  offsetof(ClarityRcasSpecData, blendMode),             sizeof(int32_t)},
            {4,  offsetof(ClarityRcasSpecData, blendIfDark),           sizeof(int32_t)},
            {5,  offsetof(ClarityRcasSpecData, blendIfLight),          sizeof(int32_t)},
            {6,  offsetof(ClarityRcasSpecData, rcasSharpness),         sizeof(float)},
            {7,  offsetof(ClarityRcasSpecData, rcasStrength),          sizeof(float)},
            {8,  offsetof(ClarityRcasSpecData, edgeThreshLow),         sizeof(float)},
            {9,  offsetof(ClarityRcasSpecData, edgeThreshHigh),        sizeof(float)},
            {10, offsetof(ClarityRcasSpecData, enableDithering),       sizeof(int32_t)},
            {11, offsetof(ClarityRcasSpecData, enableFilmGrain),       sizeof(int32_t)},
            {12, offsetof(ClarityRcasSpecData, filmGrainStrength),     sizeof(float)},
            {13, offsetof(ClarityRcasSpecData, filmGrainMinimum),      sizeof(float)},
            {14, offsetof(ClarityRcasSpecData, fineGrainWeight),       sizeof(float)},
            {15, offsetof(ClarityRcasSpecData, coarseGrainWeight),     sizeof(float)},
            {16, offsetof(ClarityRcasSpecData, hdrMode),               sizeof(int32_t)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = sizeof(mapEntries) / sizeof(mapEntries[0]);
        specializationInfo.pMapEntries   = mapEntries;
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

    void ClarityRcasEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
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

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext             = nullptr;
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

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
    }

    const std::vector<EffectParamDesc>& ClarityRcasEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"clarityRStrength",         "Strength",          ParamType::Float, 1.0,   0.0,   5.0,   0.1},
            {"clarityRBilateralRadius",  "Bilateral Radius",  ParamType::Float, 2.0,   0.5,   8.0,   0.1},
            {"clarityRBilateralOffset",  "Bilateral Offset",  ParamType::Float, 1.5,   0.5,   3.0,   0.1},
            {"clarityRBlendMode",        "Blend Mode",        ParamType::Int,   1.0,   0.0,   6.0,   1.0},
            {"clarityRBlendIfDark",      "Blend If Dark",     ParamType::Int,  40.0,   0.0, 255.0,   1.0},
            {"clarityRBlendIfLight",     "Blend If Light",    ParamType::Int, 220.0,   0.0, 255.0,   1.0},
            {"clarityRcasSharpness",     "RCAS Sharpness",    ParamType::Float, 0.8,   0.0,   2.0,  0.01},
            {"clarityRcasStrength",      "RCAS Strength",     ParamType::Float, 1.0,   0.0,   5.0,   0.1},
            {"clarityREdgeThreshLow",    "Edge Thresh Low",   ParamType::Float, 0.05,  0.0,   1.0,  0.01},
            {"clarityREdgeThreshHigh",   "Edge Thresh High",  ParamType::Float, 0.35,  0.0,   1.0,  0.01},
            {"clarityREnableDithering",  "Enable Dithering",  ParamType::Bool,  1.0,   0.0,   1.0,   1.0},
            {"clarityREnableFilmGrain",  "Enable Film Grain", ParamType::Bool,  1.0,   0.0,   1.0,   1.0},
            {"clarityRFilmGrainStrength","Grain Strength",    ParamType::Float, 1.0,   0.0,   2.0,   0.1},
            {"clarityRFilmGrainMinimum", "Grain Minimum",     ParamType::Float, 0.0,   0.0,   2.0,   0.1},
            {"clarityRFineGrainWeight",  "Fine Grain",        ParamType::Float, 0.4,   0.0,   1.0,  0.01},
            {"clarityRCoarseGrainWeight","Coarse Grain",      ParamType::Float, 0.8,   0.0,   1.0,  0.01},
        };
        return params;
    }

} // namespace vkBasalt
