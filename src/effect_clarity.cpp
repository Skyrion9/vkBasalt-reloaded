#include "effect_clarity.hpp"

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
    ClarityEffect::ClarityEffect(LogicalDevice*       pLogicalDevice,
                                 VkFormat             format,
                                 VkExtent2D           imageExtent,
                                 std::vector<VkImage> inputImages,
                                 std::vector<VkImage> outputImages,
                                 Config*              pConfig)
    {
        Logger::debug("in creating ClarityEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarity_frag;

        // Dynamic push constant size based on the ClarityPushConstants struct (.hpp).
        this->pushConstantSize = sizeof(ClarityPushConstants);

        this->radius = pConfig->getOption<float>("clarityRadius", 2.0f);
        this->offset = pConfig->getOption<float>("clarityOffset", 1.5f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        // Synced to PushVec2 struct
        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

        struct ClaritySpecData {
            float radius;
            float offset;
            float strength;
            int32_t blendMode;
            int32_t blendIfDark;
            int32_t blendIfLight;
            float edgeThreshLow;
            float edgeThreshHigh;
            int32_t enableDithering;
        };

        ClaritySpecData specData;
        specData.radius          = this->radius;
        specData.offset          = this->offset;
        specData.strength        = std::clamp(pConfig->getOption<float>("clarityStrength", 1.0f), 0.0f, 5.0f);
        specData.blendMode       = std::clamp(pConfig->getOption<int32_t>("clarityBlendMode", 1), 0, 6);
        specData.blendIfDark     = std::clamp(pConfig->getOption<int32_t>("clarityBlendIfDark", 40), 0, 255);
        specData.blendIfLight    = std::clamp(pConfig->getOption<int32_t>("clarityBlendIfLight", 220), 0, 255);
        specData.edgeThreshLow   = std::clamp(pConfig->getOption<float>("clarityEdgeThreshLow", 0.05f), 0.0f, 1.0f);
        specData.edgeThreshHigh  = std::clamp(pConfig->getOption<float>("clarityEdgeThreshHigh", 0.25f), 0.0f, 1.0f);
        specData.enableDithering = std::clamp(pConfig->getOption<int32_t>("clarityEnableDithering", 1), 0, 1);

        VkSpecializationMapEntry mapEntries[9] = {
            {0, offsetof(ClaritySpecData, radius),         sizeof(float)},
            {1, offsetof(ClaritySpecData, offset),         sizeof(float)},
            {2, offsetof(ClaritySpecData, strength),       sizeof(float)},
            {3, offsetof(ClaritySpecData, blendMode),      sizeof(int32_t)},
            {4, offsetof(ClaritySpecData, blendIfDark),    sizeof(int32_t)},
            {5, offsetof(ClaritySpecData, blendIfLight),   sizeof(int32_t)},
            {6, offsetof(ClaritySpecData, edgeThreshLow),  sizeof(float)},
            {7, offsetof(ClaritySpecData, edgeThreshHigh), sizeof(float)},
            {8, offsetof(ClaritySpecData, enableDithering),sizeof(int32_t)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 9;
        specializationInfo.pMapEntries   = mapEntries;
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
        Logger::debug("applying ClarityEffect to cb " + convertToString(commandBuffer));
        
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
        Logger::debug("after the first pipeline barrier");

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext             = nullptr;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        Logger::debug("before beginn renderpass");
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        Logger::debug("after beginn renderpass");

        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        Logger::debug("after binding image sampler");

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        Logger::debug("after bind pipeliene");

        pLogicalDevice->vkd.CmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ClarityPushConstants),
            &pushConstants
        );

        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        Logger::debug("after draw");

        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);
        Logger::debug("after end renderpass");

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                                               0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
        Logger::debug("after the second pipeline barrier");
    }

} // namespace vkBasalt