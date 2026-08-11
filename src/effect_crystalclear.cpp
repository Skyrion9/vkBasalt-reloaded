#include "effect_crystalclear.hpp"
#include <cstring>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <string>
#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "util.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    CrystalClearEffect::CrystalClearEffect(LogicalDevice*       pLogicalDevice,
                                           VkFormat             format,
                                           VkExtent2D           imageExtent,
                                           std::vector<VkImage> inputImages,
                                           std::vector<VkImage> outputImages,
                                           Config*              pConfig,
                                           VkColorSpaceKHR      colorSpace)
    {
        Logger::debug("in creating CrystalClearEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = crystalclear_frag;

        this->pushConstantSize = sizeof(CrystalClearPushConstants);
        needsUniformBuffer = true;
        uniformSize = sizeof(FrameData);

        std::string preset = pConfig->getOption<std::string>("crystalclearPreset", "devfav");
        Logger::debug("CrystalClear Preset: " + preset);

        // Base Defaults (devfav)
        float def_radius = 2.5f;
        float def_offset = 1.5f;
        float def_SharpStrength = 2.5f;
        int32_t def_blendMode = 5;
        int32_t def_blendIfDark = 8;
        int32_t def_blendIfLight = 248;
        float def_casSharpness = 1.0f;
        float def_casStrength = 3.0f;
        float def_edgeThreshLow = 0.03f;
        float def_edgeThreshHigh = 0.28f;
        float def_clarityTextureProtection = 0.35f;
        int32_t def_enableAA = 0;
        float def_fxaaEdgeThreshold = 0.05f;
        float def_fxaaSubpixAmount = 1.0f;
        float def_guardStrength = 0.4f;
        float def_bandPassWidth = 0.85f;
        float def_extremeProtection = 0.3f;
        float def_shimmerReduction = 0.4f;
        float def_vibrance = 0.0f;
        int32_t def_enableDeband = 0;
        float def_debandStrength = 0.5f;
        float def_toneCurve = 0.0f;
        int32_t def_enableChromaSmooth = 0;
        float def_chromaSmoothStrength = 0.5f;
        float def_specularDesat = 0.0f;
        int32_t def_enableFilmGrain = 1;
        float def_filmGrainStrength = 1.0f;
        float def_filmGrainMinimum = 0.0f;
        float def_fineGrainWeight = 0.4f;
        float def_coarseGrainWeight = 0.8f;

        // Override defaults based on preset
        if (preset == "esports") {
            def_SharpStrength = 2.0f; def_casStrength = 2.5f; def_guardStrength = 0.6f;
            def_extremeProtection = 0.5f; def_shimmerReduction = 0.6f; def_enableFilmGrain = 0;
            def_vibrance = 0.2f; def_blendIfDark = 15; def_blendIfLight = 240;
        } else if (preset == "artifactless") {
            def_SharpStrength = 1.2f; def_casStrength = 1.5f; def_guardStrength = 0.9f;
            def_extremeProtection = 0.8f; def_shimmerReduction = 0.8f; def_bandPassWidth = 0.6f;
            def_edgeThreshLow = 0.05f; def_edgeThreshHigh = 0.35f; def_clarityTextureProtection = 0.6f;
            def_enableFilmGrain = 0; def_enableDeband = 1; def_debandStrength = 0.4f;
            def_enableChromaSmooth = 1; def_chromaSmoothStrength = 0.4f; def_specularDesat = 0.2f;
        } else if (preset == "maxsharp") {
            def_SharpStrength = 3.5f; def_casStrength = 4.0f; def_casSharpness = 1.0f;
            def_guardStrength = 0.2f; def_extremeProtection = 0.1f; def_shimmerReduction = 0.2f;
            def_bandPassWidth = 1.2f; def_edgeThreshLow = 0.02f; def_edgeThreshHigh = 0.20f;
            def_clarityTextureProtection = 0.1f; def_filmGrainStrength = 0.8f;
        } else if (preset == "vibrantsharp") {
            def_SharpStrength = 2.5f; def_casStrength = 2.5f; def_guardStrength = 0.5f;
            def_extremeProtection = 0.4f; def_shimmerReduction = 0.5f; def_filmGrainStrength = 0.6f;
            def_enableDeband = 1; def_debandStrength = 0.6f; def_toneCurve = 0.2f;
            def_vibrance = 0.6f; def_specularDesat = 0.1f;
        } else if (preset == "devfxaa") {
            def_enableAA = 1; def_fxaaEdgeThreshold = 0.04f; def_fxaaSubpixAmount = 0.8f;
            def_SharpStrength = 2.0f; def_casStrength = 2.5f; def_guardStrength = 0.6f;
            def_filmGrainStrength = 0.8f;
        } else if (preset == "cinematic") {
            def_SharpStrength = 1.8f; def_casStrength = 1.5f; def_guardStrength = 0.7f;
            def_extremeProtection = 0.6f; def_shimmerReduction = 0.5f; def_bandPassWidth = 0.7f;
            def_filmGrainStrength = 1.2f; def_filmGrainMinimum = 0.1f; def_coarseGrainWeight = 0.9f;
            def_enableDeband = 1; def_debandStrength = 0.5f; def_toneCurve = 0.5f;
            def_vibrance = -0.1f; def_enableChromaSmooth = 1; def_chromaSmoothStrength = 0.6f;
            def_specularDesat = 0.4f; def_blendIfDark = 20; def_blendIfLight = 230;
        }
        // "devfav" or unknown falls through to base defaults

        this->radius = std::clamp(pConfig->getOption<float>("crystalclearBilateralRadius", def_radius), 0.5f, 8.0f);
        this->offset = std::clamp(pConfig->getOption<float>("crystalclearBilateralOffset", def_offset), 0.5f, 3.0f);

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        float rawOffset = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;
        pushConstants.pixelSize.x = texelSizeX;
        pushConstants.pixelSize.y = texelSizeY;

        struct CrystalClearSpecData {
            float radius; float offset; float SharpStrength; int32_t blendMode;
            int32_t blendIfDark; int32_t blendIfLight; float casSharpness; float casStrength;
            float edgeThreshLow; float edgeThreshHigh; int32_t enableDithering; int32_t enableAA;
            int32_t enableRGBEdgeDetection; float fxaaEdgeThreshold; float fxaaSubpixAmount; float fxaaSearchScale;
            float fxaaHardEdgeThreshold; float clarityTextureProtection; float fxaaEdgeThresholdMin; int32_t fxaaOnlyMode;
            int32_t enableDebugAA; int32_t enableDebugCAS; int32_t enableDebugClarity; int32_t enableFilmGrain;
            float filmGrainStrength; float filmGrainMinimum; int32_t enableDebugGrain; float fineGrainWeight;
            float coarseGrainWeight; int32_t hdrMode; float guardStrength; float bandPassWidth;
            float extremeProtection; float shimmerReduction; float vibrance; int32_t enableDeband;
            float debandStrength; float toneCurve; int32_t enableChromaSmooth; float chromaSmoothStrength;
            float specularDesat;
        };

        CrystalClearSpecData specData;

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        // Read from config, store in m_paramValues, return the value
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

        // Populate m_paramValues and specData simultaneously
        specData.radius                    = std::clamp((float)this->radius, 0.5f, 8.0f);
        m_paramValues["crystalclearBilateralRadius"] = specData.radius;
        specData.offset                    = std::clamp((float)this->offset, 0.5f, 3.0f);
        m_paramValues["crystalclearBilateralOffset"] = specData.offset;
        specData.SharpStrength             = std::clamp((float)getAndStore("crystalclearSharpStrength", def_SharpStrength), 0.0f, 5.0f);
        specData.blendMode                 = std::clamp((int32_t)getAndStoreInt("crystalclearBlendMode", def_blendMode), int32_t(0), int32_t(6));
        specData.blendIfDark               = std::clamp((int32_t)getAndStoreInt("crystalclearBlendIfDark", def_blendIfDark), int32_t(0), int32_t(255));
        specData.blendIfLight              = std::clamp((int32_t)getAndStoreInt("crystalclearBlendIfLight", def_blendIfLight), int32_t(0), int32_t(255));
        specData.casSharpness              = std::clamp((float)getAndStore("crystalclearCasSharpness", def_casSharpness), 0.0f, 1.0f);
        specData.casStrength               = std::clamp((float)getAndStore("crystalclearCasStrength", def_casStrength), 0.0f, 5.0f);
        specData.edgeThreshLow             = std::clamp((float)getAndStore("crystalclearEdgeThreshLow", def_edgeThreshLow), 0.0f, 1.0f);
        specData.edgeThreshHigh            = std::clamp((float)getAndStore("crystalclearEdgeThreshHigh", def_edgeThreshHigh), 0.0f, 1.0f);
        specData.enableDithering           = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDithering", 1), int32_t(0), int32_t(1));
        specData.enableAA                  = std::clamp((int32_t)getAndStoreInt("crystalclearEnableAA", def_enableAA), int32_t(0), int32_t(1));
        specData.enableRGBEdgeDetection    = std::clamp((int32_t)getAndStoreInt("crystalclearEnableRGBEdgeDetection", 1), int32_t(0), int32_t(1));
        specData.fxaaEdgeThreshold         = std::clamp((float)getAndStore("crystalclearFxaaEdgeThreshold", def_fxaaEdgeThreshold), 0.001f, 1.0f);
        specData.fxaaSubpixAmount          = std::clamp((float)getAndStore("crystalclearFxaaSubpixAmount", def_fxaaSubpixAmount), 0.0f, 1.0f);
        specData.fxaaSearchScale           = std::clamp((float)getAndStore("crystalclearFxaaSearchScale", 1.0f), 0.1f, 3.0f);
        specData.fxaaHardEdgeThreshold     = std::clamp((float)getAndStore("crystalclearFxaaHardEdgeThreshold", 0.08f), 0.0f, 1.0f);
        specData.clarityTextureProtection  = std::clamp((float)getAndStore("crystalclearClarityTextureProtection", def_clarityTextureProtection), 0.0f, 1.0f);
        specData.fxaaEdgeThresholdMin      = std::clamp((float)getAndStore("crystalclearFxaaEdgeThresholdMin", 0.0312f), 0.0f, 1.0f);
        specData.fxaaOnlyMode              = std::clamp((int32_t)getAndStoreInt("crystalclearFxaaOnlyMode", 0), int32_t(0), int32_t(1));
        specData.enableDebugAA             = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDebugAA", 0), int32_t(0), int32_t(1));
        specData.enableDebugCAS            = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDebugCAS", 0), int32_t(0), int32_t(1));
        specData.enableDebugClarity        = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDebugClarity", 0), int32_t(0), int32_t(1));
        specData.enableFilmGrain           = std::clamp((int32_t)getAndStoreInt("crystalclearEnableFilmGrain", def_enableFilmGrain), int32_t(0), int32_t(1));
        specData.filmGrainStrength         = std::clamp((float)getAndStore("crystalclearFilmGrainStrength", def_filmGrainStrength), 0.0f, 2.0f);
        specData.filmGrainMinimum          = std::clamp((float)getAndStore("crystalclearFilmGrainMinimum", def_filmGrainMinimum), 0.0f, 2.0f);
        specData.enableDebugGrain          = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDebugGrain", 0), int32_t(0), int32_t(1));
        specData.fineGrainWeight           = std::clamp((float)getAndStore("crystalclearFineGrainWeight", def_fineGrainWeight), 0.0f, 1.0f);
        specData.coarseGrainWeight         = std::clamp((float)getAndStore("crystalclearCoarseGrainWeight", def_coarseGrainWeight), 0.0f, 1.0f);
        specData.hdrMode                   = isHDR ? 1 : 0;
        specData.guardStrength             = std::clamp((float)getAndStore("crystalclearGuardStrength", def_guardStrength), 0.0f, 1.0f);
        specData.bandPassWidth             = std::clamp((float)getAndStore("crystalclearBandPassWidth", def_bandPassWidth), 0.3f, 1.5f);
        specData.extremeProtection         = std::clamp((float)getAndStore("crystalclearExtremeProtection", def_extremeProtection), 0.0f, 1.0f);
        specData.shimmerReduction          = std::clamp((float)getAndStore("crystalclearShimmerReduction", def_shimmerReduction), 0.0f, 1.0f);
        specData.vibrance                  = std::clamp((float)getAndStore("crystalclearVibrance", def_vibrance), -1.0f, 1.0f);
        specData.enableDeband              = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDeband", def_enableDeband), int32_t(0), int32_t(1));
        specData.debandStrength            = std::clamp((float)getAndStore("crystalclearDebandStrength", def_debandStrength), 0.0f, 1.0f);
        specData.toneCurve                 = std::clamp((float)getAndStore("crystalclearToneCurve", def_toneCurve), 0.0f, 1.0f);
        specData.enableChromaSmooth        = std::clamp((int32_t)getAndStoreInt("crystalclearEnableChromaSmooth", def_enableChromaSmooth), int32_t(0), int32_t(1));
        specData.chromaSmoothStrength      = std::clamp((float)getAndStore("crystalclearChromaSmoothStrength", def_chromaSmoothStrength), 0.0f, 1.0f);
        specData.specularDesat             = std::clamp((float)getAndStore("crystalclearSpecularDesat", def_specularDesat), 0.0f, 1.0f);

        VkSpecializationMapEntry mapEntries[41] = {
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
            {27, offsetof(CrystalClearSpecData, fineGrainWeight),           sizeof(float)},
            {28, offsetof(CrystalClearSpecData, coarseGrainWeight),         sizeof(float)},
            {29, offsetof(CrystalClearSpecData, hdrMode),                   sizeof(int32_t)},
            {30, offsetof(CrystalClearSpecData, guardStrength),             sizeof(float)},
            {31, offsetof(CrystalClearSpecData, bandPassWidth),             sizeof(float)},
            {32, offsetof(CrystalClearSpecData, extremeProtection),         sizeof(float)},
            {33, offsetof(CrystalClearSpecData, shimmerReduction),          sizeof(float)},
            {34, offsetof(CrystalClearSpecData, vibrance),                  sizeof(float)},
            {35, offsetof(CrystalClearSpecData, enableDeband),              sizeof(int32_t)},
            {36, offsetof(CrystalClearSpecData, debandStrength),            sizeof(float)},
            {37, offsetof(CrystalClearSpecData, toneCurve),                 sizeof(float)},
            {38, offsetof(CrystalClearSpecData, enableChromaSmooth),        sizeof(int32_t)},
            {39, offsetof(CrystalClearSpecData, chromaSmoothStrength),      sizeof(float)},
            {40, offsetof(CrystalClearSpecData, specularDesat),             sizeof(float)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 41;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(CrystalClearSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    CrystalClearEffect::~CrystalClearEffect() {}

    void CrystalClearEffect::updateEffect() {
        if (mappedUniform) {
            FrameData* data = static_cast<FrameData*>(mappedUniform);
            data->frameCounter = m_frameCounter++;
        }
    }

    void CrystalClearEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) {
        Logger::debug("applying CrystalClearEffect to cb " + convertToString(commandBuffer));

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

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

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

        pLogicalDevice->vkd.CmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CrystalClearPushConstants), &pushConstants);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
    }

    // Declarative parameter interface
    const std::vector<EffectParamDesc>& CrystalClearEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"crystalclearPreset", "Preset", ParamType::Combo, 0.0, 0.0, 0.0, 0.0, 
                {"devfav", "esports", "artifactless", "maxsharp", "vibrantsharp", "devfxaa", "cinematic"}},
            {"crystalclearBilateralRadius",       "Bilateral Radius",       ParamType::Float, 2.5,   0.5,   8.0,   0.1},
            {"crystalclearBilateralOffset",       "Bilateral Offset",       ParamType::Float, 1.5,   0.5,   3.0,   0.1},
            {"crystalclearSharpStrength",         "Sharp Strength",         ParamType::Float, 2.5,   0.0,   5.0,   0.1},
            {"crystalclearBlendMode",             "Blend Mode",             ParamType::Int,   5.0,   0.0,   6.0,   1.0},
            {"crystalclearBlendIfDark",           "Blend If Dark",          ParamType::Int,   8.0,   0.0, 255.0,   1.0},
            {"crystalclearBlendIfLight",          "Blend If Light",         ParamType::Int, 248.0,   0.0, 255.0,   1.0},
            {"crystalclearCasSharpness",          "CAS Sharpness",          ParamType::Float, 1.0,   0.0,   1.0,  0.01},
            {"crystalclearCasStrength",           "CAS Strength",           ParamType::Float, 3.0,   0.0,   5.0,   0.1},
            {"crystalclearEdgeThreshLow",         "Edge Thresh Low",        ParamType::Float, 0.03,  0.0,   1.0,  0.01},
            {"crystalclearEdgeThreshHigh",        "Edge Thresh High",       ParamType::Float, 0.28,  0.0,   1.0,  0.01},
            {"crystalclearEnableDithering",       "Enable Dithering",       ParamType::Bool,  1.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableAA",              "Enable AA",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableRGBEdgeDetection","RGB Edge Detection",     ParamType::Bool,  1.0,   0.0,   1.0,   1.0},
            {"crystalclearFxaaEdgeThreshold",     "FXAA Edge Thresh",       ParamType::Float, 0.05,  0.001, 1.0,  0.01},
            {"crystalclearFxaaSubpixAmount",      "FXAA Subpix",            ParamType::Float, 1.0,   0.0,   1.0,  0.01},
            {"crystalclearFxaaSearchScale",       "FXAA Search Scale",      ParamType::Float, 1.0,   0.1,   3.0,   0.1},
            {"crystalclearFxaaHardEdgeThreshold", "FXAA Hard Edge",         ParamType::Float, 0.08,  0.0,   1.0,  0.01},
            {"crystalclearClarityTextureProtection","Clarity Protection",   ParamType::Float, 0.35,  0.0,   1.0,  0.01},
            {"crystalclearFxaaEdgeThresholdMin",  "FXAA Edge Min",          ParamType::Float, 0.0312,0.0,   1.0, 0.001},
            {"crystalclearFxaaOnlyMode",          "FXAA Only",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableDebugAA",         "Debug AA",               ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableDebugCAS",        "Debug CAS",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableDebugClarity",    "Debug Clarity",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearEnableFilmGrain",       "Enable Film Grain",      ParamType::Bool,  1.0,   0.0,   1.0,   1.0},
            {"crystalclearFilmGrainStrength",     "Grain Strength",         ParamType::Float, 1.0,   0.0,   2.0,   0.1},
            {"crystalclearFilmGrainMinimum",      "Grain Minimum",          ParamType::Float, 0.0,   0.0,   2.0,   0.1},
            {"crystalclearEnableDebugGrain",      "Debug Grain",            ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearFineGrainWeight",       "Fine Grain",             ParamType::Float, 0.4,   0.0,   1.0,  0.01},
            {"crystalclearCoarseGrainWeight",     "Coarse Grain",           ParamType::Float, 0.8,   0.0,   1.0,  0.01},
            {"crystalclearGuardStrength",         "Guard Strength",         ParamType::Float, 0.4,   0.0,   1.0,  0.01},
            {"crystalclearBandPassWidth",         "Band Pass Width",        ParamType::Float, 0.85,  0.3,   1.5,  0.05},
            {"crystalclearExtremeProtection",     "Extreme Protection",     ParamType::Float, 0.3,   0.0,   1.0,  0.01},
            {"crystalclearShimmerReduction",      "Shimmer Reduction",      ParamType::Float, 0.4,   0.0,   1.0,  0.01},
            {"crystalclearVibrance",              "Vibrance",               ParamType::Float, 0.0,  -1.0,   1.0,  0.05},
            {"crystalclearEnableDeband",          "Enable Deband",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearDebandStrength",        "Deband Strength",        ParamType::Float, 0.5,   0.0,   1.0,  0.05},
            {"crystalclearToneCurve",             "Tone Curve",             ParamType::Float, 0.0,   0.0,   1.0,  0.01},
            {"crystalclearEnableChromaSmooth",    "Chroma Smooth",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0},
            {"crystalclearChromaSmoothStrength",  "Chroma Strength",        ParamType::Float, 0.5,   0.0,   1.0,  0.01},
            {"crystalclearSpecularDesat",         "Specular Desat",         ParamType::Float, 0.0,   0.0,   1.0,  0.01},
        };
        return params;
    }

    double CrystalClearEffect::getParam(const std::string& key) const {
        auto it = m_paramValues.find(key);
        return (it != m_paramValues.end()) ? it->second : 0.0;
    }

    bool CrystalClearEffect::setParam(const std::string& key, double value) {
        auto it = m_paramValues.find(key);
        if (it == m_paramValues.end()) return false;
        if (it->second == value) return false;
        it->second = value;
        return true;
    }
} // namespace vkBasalt
