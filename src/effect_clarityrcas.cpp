#include "effect_clarityrcas.hpp"

#include <cstring>
#include <cmath>
#include <cstddef> 
#include <algorithm>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "util.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
    ClarityRcasEffect::ClarityRcasEffect(LogicalDevice*       pLogicalDevice,
                                         VkFormat             format,
                                         VkExtent2D           imageExtent,
                                         std::vector<VkImage> inputImages,
                                         std::vector<VkImage> outputImages,
                                         Config*              pConfig)
    {
        Logger::debug("in creating ClarityRcasEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarityrcas_frag;

        // Dynamic push constant size based on the ClarityRcasPushConstants struct (.hpp).
        this->pushConstantSize = sizeof(ClarityRcasPushConstants);

        // Request UBO for per-frame temporal seed
        needsUniformBuffer = true;
        uniformSize = sizeof(FrameData);

        this->radius = pConfig->getOption<float>("clarityRBilateralRadius", 2.0f);
        this->offset = pConfig->getOption<float>("clarityRBilateralOffset", 1.5f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        // Synced to PushVec2 struct
        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

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
        };

        ClarityRcasSpecData specData;
        specData.radius               = this->radius;
        specData.offset               = this->offset;
        specData.clarityStrength      = std::clamp(pConfig->getOption<float>("clarityRStrength", 1.0f), 0.0f, 5.0f);
        specData.blendMode            = std::clamp(pConfig->getOption<int32_t>("clarityRBlendMode", 1), 0, 6);
        specData.blendIfDark          = std::clamp(pConfig->getOption<int32_t>("clarityRBlendIfDark", 40), 0, 255);
        specData.blendIfLight         = std::clamp(pConfig->getOption<int32_t>("clarityRBlendIfLight", 220), 0, 255);
        specData.rcasSharpness        = std::clamp(pConfig->getOption<float>("clarityRcasSharpness", 0.8f), 0.0f, 2.0f);
        specData.rcasStrength         = std::clamp(pConfig->getOption<float>("clarityRcasStrength", 1.0f), 0.0f, 5.0f);
        specData.edgeThreshLow        = std::clamp(pConfig->getOption<float>("clarityREdgeThreshLow", 0.05f), 0.0f, 1.0f);
        specData.edgeThreshHigh       = std::clamp(pConfig->getOption<float>("clarityREdgeThreshHigh", 0.35f), 0.0f, 1.0f);
        specData.enableDithering      = std::clamp(pConfig->getOption<int32_t>("clarityREnableDithering", 1), 0, 1);
        
        specData.enableFilmGrain      = std::clamp(pConfig->getOption<int32_t>("clarityREnableFilmGrain", 1), 0, 1);
        specData.filmGrainStrength    = std::clamp(pConfig->getOption<float>("clarityRFilmGrainStrength", 1.0f), 0.0f, 2.0f);
        specData.filmGrainMinimum     = std::clamp(pConfig->getOption<float>("clarityRFilmGrainMinimum", 0.0f), 0.0f, 2.0f);
        specData.fineGrainWeight      = std::clamp(pConfig->getOption<float>("clarityRFineGrainWeight", 0.4f), 0.0f, 1.0f);
        specData.coarseGrainWeight    = std::clamp(pConfig->getOption<float>("clarityRCoarseGrainWeight", 0.8f), 0.0f, 1.0f);

        VkSpecializationMapEntry mapEntries[16] = {
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
            {15, offsetof(ClarityRcasSpecData, coarseGrainWeight),     sizeof(float)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 16;
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
        Logger::debug("applying ClarityRcasEffect to cb " + convertToString(commandBuffer));
        
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

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        Logger::debug("after the first pipeline barrier");

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

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
        Logger::debug("after the second pipeline barrier");
    }
} // namespace vkBasalt
