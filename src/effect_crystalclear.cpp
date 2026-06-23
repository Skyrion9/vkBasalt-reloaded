#include "effect_crystalclear.hpp"

#include <cstring>
#include <cmath>
#include <cstddef> // Required for offsetof
#include <algorithm> // Required for std::clamp

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
    CrystalClearEffect::CrystalClearEffect(LogicalDevice*       pLogicalDevice,
                                     VkFormat             format,
                                     VkExtent2D           imageExtent,
                                     std::vector<VkImage> inputImages,
                                     std::vector<VkImage> outputImages,
                                     Config*              pConfig)
    {
        Logger::debug("in creating CrystalClearEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = crystalclear_frag;

        // Dynamic push constant size based on the CrystalClearPushConstants struct (.hpp).
        this->pushConstantSize = sizeof(CrystalClearPushConstants);

        // Request UBO for per-frame temporal seed
        needsUniformBuffer = true;
        uniformSize = sizeof(FrameData);

        // Store radius and offset for push constant calculation. Clamped to pass safe values.
        this->radius = std::clamp(pConfig->getOption<float>("crystalclearBilateralRadius", 3.5f), 0.5f, 8.0f);
        this->offset = std::clamp(pConfig->getOption<float>("crystalclearBilateralOffset", 1.5f), 0.5f, 3.0f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        // The Bilinear Guarantee & Offset Math
        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;

        pushConstants.pixelSize.x = texelSizeX;
        pushConstants.pixelSize.y = texelSizeY;

        struct CrystalClearSpecData {
            float radius;
            float offset;
            float SharpStrength;
            int32_t blendMode;
            int32_t blendIfDark;
            int32_t blendIfLight;
            float casSharpness;
            float casStrength;
            float edgeThreshLow;
            float edgeThreshHigh;
            int32_t enableDithering;
            int32_t enableAA;
            int32_t enableRGBEdgeDetection;
            float fxaaEdgeThreshold;
            float fxaaSubpixAmount;
            float fxaaSearchScale;
            float fxaaHardEdgeThreshold;
            float clarityTextureProtection;
            float fxaaEdgeThresholdMin;
            int32_t fxaaOnlyMode;
            int32_t enableDebugAA;
            int32_t enableDebugCAS;
            int32_t enableDebugClarity;
            int32_t enableFilmGrain;
            float filmGrainStrength;
            float filmGrainMinimum;
            int32_t enableDebugGrain;
            float fineGrainWeight;
            float coarseGrainWeight;
        };

        CrystalClearSpecData specData;
        
        // Clamp values to sane ranges
        specData.radius                    = std::clamp(this->radius, 0.5f, 8.0f);
        specData.offset                    = std::clamp(this->offset, 0.5f, 3.0f);
        specData.SharpStrength             = std::clamp(pConfig->getOption<float>("crystalclearSharpStrength", 5.0f), 0.0f, 10.0f);
        specData.blendMode                 = std::clamp(pConfig->getOption<int32_t>("crystalclearBlendMode", 1), int32_t(0), int32_t(6));
        specData.blendIfDark               = std::clamp(pConfig->getOption<int32_t>("crystalclearBlendIfDark", 40), int32_t(0), int32_t(255));
        specData.blendIfLight              = std::clamp(pConfig->getOption<int32_t>("crystalclearBlendIfLight", 220), int32_t(0), int32_t(255));
        specData.casSharpness              = std::clamp(pConfig->getOption<float>("crystalclearCasSharpness", 1.0f), 0.0f, 1.0f);
        specData.casStrength               = std::clamp(pConfig->getOption<float>("crystalclearCasStrength", 5.0f), 0.0f, 10.0f);
        specData.edgeThreshLow             = std::clamp(pConfig->getOption<float>("crystalclearEdgeThreshLow", 0.05f), 0.0f, 1.0f);
        specData.edgeThreshHigh            = std::clamp(pConfig->getOption<float>("crystalclearEdgeThreshHigh", 0.35f), 0.0f, 1.0f);
        specData.enableDithering           = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableDithering", 1), int32_t(0), int32_t(1));
        specData.enableAA                  = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableAA", 1), int32_t(0), int32_t(1));
        specData.enableRGBEdgeDetection    = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableRGBEdgeDetection", 1), int32_t(0), int32_t(1));
        specData.fxaaEdgeThreshold         = std::clamp(pConfig->getOption<float>("crystalclearFxaaEdgeThreshold", 0.0625f), 0.001f, 1.0f);
        specData.fxaaSubpixAmount          = std::clamp(pConfig->getOption<float>("crystalclearFxaaSubpixAmount", 0.75f), 0.0f, 1.0f);
        specData.fxaaSearchScale           = std::clamp(pConfig->getOption<float>("crystalclearFxaaSearchScale", 1.0f), 0.1f, 3.0f);
        specData.fxaaHardEdgeThreshold     = std::clamp(pConfig->getOption<float>("crystalclearFxaaHardEdgeThreshold", 0.08f), 0.0f, 1.0f);
        specData.clarityTextureProtection  = std::clamp(pConfig->getOption<float>("crystalclearClarityTextureProtection", 0.5f), 0.0f, 1.0f);
        specData.fxaaEdgeThresholdMin      = std::clamp(pConfig->getOption<float>("crystalclearFxaaEdgeThresholdMin", 0.0312f), 0.0f, 1.0f);
        specData.fxaaOnlyMode              = std::clamp(pConfig->getOption<int32_t>("crystalclearFxaaOnlyMode", 0), int32_t(0), int32_t(1));
        specData.enableDebugAA             = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableDebugAA", 0), int32_t(0), int32_t(1));
        specData.enableDebugCAS            = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableDebugCAS", 0), int32_t(0), int32_t(1));
        specData.enableDebugClarity        = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableDebugClarity", 0), int32_t(0), int32_t(1));
        specData.enableFilmGrain           = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableFilmGrain", 1), int32_t(0), int32_t(1));
        specData.filmGrainStrength         = std::clamp(pConfig->getOption<float>("crystalclearFilmGrainStrength", 0.5f), 0.0f, 2.0f);
        specData.filmGrainMinimum          = std::clamp(pConfig->getOption<float>("crystalclearFilmGrainMinimum", 0.15f), 0.0f, 2.0f);
        specData.enableDebugGrain          = std::clamp(pConfig->getOption<int32_t>("crystalclearEnableDebugGrain", 0), int32_t(0), int32_t(1));
        specData.fineGrainWeight           = std::clamp(pConfig->getOption<float>("crystalclearFineGrainWeight", 0.6f), 0.0f, 1.0f);
        specData.coarseGrainWeight         = std::clamp(pConfig->getOption<float>("crystalclearCoarseGrainWeight", 0.4f), 0.0f, 1.0f);

        // Map struct fields to GLSL constant IDs using offsetof
        VkSpecializationMapEntry mapEntries[29] = {
            {0,  offsetof(CrystalClearSpecData, radius),                    sizeof(float)},
            {1,  offsetof(CrystalClearSpecData, offset),                    sizeof(float)},
            {2,  offsetof(CrystalClearSpecData, SharpStrength),             sizeof(float)},
            {3,  offsetof(CrystalClearSpecData, blendMode),                 sizeof(int32_t)},
            {4,  offsetof(CrystalClearSpecData, blendIfDark),               sizeof(int32_t)},
            {5,  offsetof(CrystalClearSpecData, blendIfLight),              sizeof(int32_t)},
            {6,  offsetof(CrystalClearSpecData, casSharpness),              sizeof(float)},
            {7,  offsetof(CrystalClearSpecData, casStrength),               sizeof(float)},
            {8,  offsetof(CrystalClearSpecData, edgeThreshLow),             sizeof(float)},
            {9,  offsetof(CrystalClearSpecData, edgeThreshHigh),            sizeof(float)},
            {10, offsetof(CrystalClearSpecData, enableDithering),           sizeof(int32_t)},
            {11, offsetof(CrystalClearSpecData, enableAA),                  sizeof(int32_t)},
            {12, offsetof(CrystalClearSpecData, enableRGBEdgeDetection),    sizeof(int32_t)},
            {13, offsetof(CrystalClearSpecData, fxaaEdgeThreshold),         sizeof(float)},
            {14, offsetof(CrystalClearSpecData, fxaaSubpixAmount),          sizeof(float)},
            {15, offsetof(CrystalClearSpecData, fxaaSearchScale),           sizeof(float)},
            {16, offsetof(CrystalClearSpecData, fxaaHardEdgeThreshold),     sizeof(float)},
            {17, offsetof(CrystalClearSpecData, clarityTextureProtection),  sizeof(float)},
            {18, offsetof(CrystalClearSpecData, fxaaEdgeThresholdMin),      sizeof(float)},
            {19, offsetof(CrystalClearSpecData, fxaaOnlyMode),              sizeof(int32_t)},
            {20, offsetof(CrystalClearSpecData, enableDebugAA),             sizeof(int32_t)},
            {21, offsetof(CrystalClearSpecData, enableDebugCAS),            sizeof(int32_t)},
            {22, offsetof(CrystalClearSpecData, enableDebugClarity),        sizeof(int32_t)},
            {23, offsetof(CrystalClearSpecData, enableFilmGrain),           sizeof(int32_t)},
            {24, offsetof(CrystalClearSpecData, filmGrainStrength),         sizeof(float)},
            {25, offsetof(CrystalClearSpecData, filmGrainMinimum),          sizeof(float)},
            {26, offsetof(CrystalClearSpecData, enableDebugGrain),          sizeof(int32_t)},
            {27, offsetof(CrystalClearSpecData, fineGrainWeight),      sizeof(float)},
            {28, offsetof(CrystalClearSpecData, coarseGrainWeight),    sizeof(float)}
        };

        // Setup specialization info with correct data size
        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 29;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(CrystalClearSpecData);
        specializationInfo.pData         = &specData;

        // Assign to SimpleEffect's protected pointers before calling base init
        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        // Call base class init to setup pipelines, framebuffers, etc.
        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    CrystalClearEffect::~CrystalClearEffect()
    {
    }

    void CrystalClearEffect::updateEffect()
    {
        // Write the new frame counter directly to the mapped GPU memory. We use this to randomize noise.
        // The pre-recorded command buffer will automatically read this new value next frame.
        if (mappedUniform) {
            FrameData* data = static_cast<FrameData*>(mappedUniform);
            data->frameCounter = m_frameCounter++;
        }
    }

    void CrystalClearEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        Logger::debug("applying CrystalClearEffect to cb " + convertToString(commandBuffer));
        
        // Used to make the Image accessable by the shader
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

        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        // Reverses the first Barrier
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

        secondBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        secondBarrier.subresourceRange.baseMipLevel   = 0;
        secondBarrier.subresourceRange.levelCount     = 1;
        secondBarrier.subresourceRange.baseArrayLayer = 0;
        secondBarrier.subresourceRange.layerCount     = 1;

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

        // Push constant injection
        pLogicalDevice->vkd.CmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(CrystalClearPushConstants),
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
