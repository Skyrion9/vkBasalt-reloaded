#include "effect_crystalclear.hpp"

#include <array>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>
#include <unordered_map>
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
        int32_t hdrMode;
        float guardStrength;
        float bandPassWidth;
        float extremeProtection;
        float shimmerReduction;
        float vibrance;
        int32_t enableDeband;
        float debandStrength;
        float toneCurve;
        int32_t enableChromaSmooth;
        float chromaSmoothStrength;
        float specularDesat;
        float localContrastStrength;
        int32_t enableDespeckle;
        float despeckleThreshold;
        int32_t enableFringeFix;
        float fringeStrength;
        float saturation;
        int32_t enableCDL;
        float cdlSlopeR;
        float cdlSlopeG;
        float cdlSlopeB;
        float cdlOffsetR;
        float cdlOffsetG;
        float cdlOffsetB;
        float cdlPowerR;
        float cdlPowerG;
        float cdlPowerB;
        int32_t enableSplitTone;
        float stShadowR;
        float stShadowG;
        float stShadowB;
        float stHighR;
        float stHighG;
        float stHighB;
        float splitToneStrength;
        float temperature;
        float tint;
        float gammaAdjust;
        float blackLift;
        float whiteClip;
        int32_t enableCheckerboardFix;
        float checkerboardStrength;
        int32_t liteMode;
    };

    #define SPEC(id, field) id, offsetof(CrystalClearSpecData, field), sizeof(((CrystalClearSpecData*)0)->field)

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
        m_paramValues["crystalclearPreset"] = 0.0; // Combo type, value unused by getParam

        using PresetMap = std::unordered_map<std::string, double>;
        static const std::unordered_map<std::string, PresetMap> presetTable = {
            {"esports", {
                {"crystalclearSharpStrength", 2.0}, {"crystalclearCasStrength", 2.5},
                {"crystalclearGuardStrength", 0.6}, {"crystalclearExtremeProtection", 0.5},
                {"crystalclearShimmerReduction", 0.6}, {"crystalclearFilmGrainStrength", 0.8},
                {"crystalclearEnableFilmGrain", 0}, {"crystalclearToneCurve", 0.2},
                {"crystalclearVibrance", 0.0}, {"crystalclearBlendIfDark", 15},
                {"crystalclearBlendIfLight", 240}
            }},
            {"artifactless", {
                {"crystalclearSharpStrength", 1.2}, {"crystalclearCasStrength", 1.5},
                {"crystalclearGuardStrength", 0.9}, {"crystalclearExtremeProtection", 0.8},
                {"crystalclearShimmerReduction", 0.8}, {"crystalclearBandPassWidth", 0.6},
                {"crystalclearEdgeThreshLow", 0.05}, {"crystalclearEdgeThreshHigh", 0.35},
                {"crystalclearClarityTextureProtection", 0.6},
                {"crystalclearEnableFilmGrain", 0}, {"crystalclearEnableDeband", 1},
                {"crystalclearDebandStrength", 0.4}, {"crystalclearEnableChromaSmooth", 1},
                {"crystalclearChromaSmoothStrength", 0.4}, {"crystalclearSpecularDesat", 0.2}
            }},
            {"maxsharp", {
                {"crystalclearSharpStrength", 3.5}, {"crystalclearCasStrength", 4.0},
                {"crystalclearCasSharpness", 1.0}, {"crystalclearGuardStrength", 0.2},
                {"crystalclearExtremeProtection", 0.1}, {"crystalclearShimmerReduction", 0.2},
                {"crystalclearBandPassWidth", 1.2}, {"crystalclearEdgeThreshLow", 0.02},
                {"crystalclearEdgeThreshHigh", 0.20}, {"crystalclearClarityTextureProtection", 0.1},
                {"crystalclearFilmGrainStrength", 0.8}
            }},
            {"vibrantsharp", {
                {"crystalclearSharpStrength", 2.5}, {"crystalclearCasStrength", 2.5},
                {"crystalclearGuardStrength", 0.5}, {"crystalclearExtremeProtection", 0.4},
                {"crystalclearShimmerReduction", 0.5}, {"crystalclearFilmGrainStrength", 0.6},
                {"crystalclearEnableDeband", 1}, {"crystalclearDebandStrength", 0.6},
                {"crystalclearToneCurve", 0.2}, {"crystalclearVibrance", 0.6},
                {"crystalclearSpecularDesat", 0.1}
            }},
            {"devfxaa", {
                {"crystalclearSharpStrength", 2.0}, {"crystalclearCasStrength", 2.5},
                {"crystalclearGuardStrength", 0.6}, {"crystalclearEnableAA", 1},
                {"crystalclearFxaaEdgeThreshold", 0.04}, {"crystalclearFxaaSubpixAmount", 0.8},
                {"crystalclearFilmGrainStrength", 0.8}
            }},
            {"cinematic", {
                {"crystalclearSharpStrength", 1.8}, {"crystalclearCasStrength", 1.5},
                {"crystalclearGuardStrength", 0.7}, {"crystalclearExtremeProtection", 0.6},
                {"crystalclearShimmerReduction", 0.5}, {"crystalclearBandPassWidth", 0.7},
                {"crystalclearFilmGrainStrength", 1.2}, {"crystalclearFilmGrainMinimum", 0.1},
                {"crystalclearCoarseGrainWeight", 0.9}, {"crystalclearEnableDeband", 1},
                {"crystalclearDebandStrength", 0.5}, {"crystalclearToneCurve", 0.5},
                {"crystalclearVibrance", -0.1}, {"crystalclearEnableChromaSmooth", 1},
                {"crystalclearChromaSmoothStrength", 0.6}, {"crystalclearSpecularDesat", 0.4},
                {"crystalclearBlendIfDark", 20}, {"crystalclearBlendIfLight", 230},
                {"crystalclearEnableSplitTone", 1}, {"crystalclearSplitToneStrength", 0.3},
                {"crystalclearSTShadowR", 0.0}, {"crystalclearSTShadowG", 0.4},
                {"crystalclearSTShadowB", 0.5}, {"crystalclearSTHighR", 0.5},
                {"crystalclearSTHighG", 0.3}, {"crystalclearSTHighB", 0.0},
                {"crystalclearTemperature", 0.1}
            }},
            {"film", {
                {"crystalclearSharpStrength", 1.5}, {"crystalclearCasStrength", 1.5},
                {"crystalclearGuardStrength", 0.7}, {"crystalclearExtremeProtection", 0.7},
                {"crystalclearShimmerReduction", 0.6}, {"crystalclearFilmGrainStrength", 1.5},
                {"crystalclearFilmGrainMinimum", 0.2}, {"crystalclearCoarseGrainWeight", 0.9},
                {"crystalclearEnableDeband", 1}, {"crystalclearDebandStrength", 0.5},
                {"crystalclearToneCurve", 0.4}, {"crystalclearSaturation", -0.2},
                {"crystalclearEnableSplitTone", 1}, {"crystalclearSplitToneStrength", 0.25},
                {"crystalclearSTShadowR", 0.0}, {"crystalclearSTShadowG", 0.3},
                {"crystalclearSTShadowB", 0.4}, {"crystalclearSTHighR", 0.4},
                {"crystalclearSTHighG", 0.25}, {"crystalclearSTHighB", 0.0},
                {"crystalclearTemperature", 0.05}, {"crystalclearGammaAdjust", -0.1},
                {"crystalclearBlackLift", 0.15}, {"crystalclearWhiteClip", 0.1}
            }},
            {"vivid", {
                {"crystalclearSharpStrength", 2.5}, {"crystalclearCasStrength", 3.0},
                {"crystalclearGuardStrength", 0.4}, {"crystalclearFilmGrainStrength", 0.6},
                {"crystalclearVibrance", 0.3}, {"crystalclearSaturation", 0.4},
                {"crystalclearTemperature", 0.05}, {"crystalclearEnableCDL", 1},
                {"crystalclearCDLSlopeR", 1.1}, {"crystalclearCDLSlopeG", 1.05},
                {"crystalclearCDLSlopeB", 1.0}
            }},
            {"noir", {
                {"crystalclearSharpStrength", 3.0}, {"crystalclearCasStrength", 3.5},
                {"crystalclearGuardStrength", 0.3}, {"crystalclearExtremeProtection", 0.2},
                {"crystalclearShimmerReduction", 0.3}, {"crystalclearFilmGrainStrength", 1.0},
                {"crystalclearToneCurve", 0.3}, {"crystalclearSaturation", -1.0},
                {"crystalclearGammaAdjust", 0.2}, {"crystalclearBlackLift", 0.05}
            }},
        };

        PresetMap activePreset;
        auto presetIt = presetTable.find(preset);
        if (presetIt != presetTable.end()) {
            activePreset = presetIt->second;
        }

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        // read config, apply presets, clamp, write to specData by offset, and build mapEntries
        const auto& params = getParamDescs();
        CrystalClearSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double def = p.defaultVal;
            auto overrideIt = activePreset.find(p.key);
            if (overrideIt != activePreset.end()) {
                def = overrideIt->second;
            }

            double val;
            if (p.type == ParamType::Float) {
                val = (double)pConfig->getOption<float>(p.key, (float)def);
            } else {
                val = (double)pConfig->getOption<int32_t>(p.key, (int32_t)def);
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
        mapEntries.push_back({29, offsetof(CrystalClearSpecData, hdrMode), sizeof(int32_t)});

        // Lite mode disable expensive features at the spec constant level. The compiler strips these blocks entirely from the compiled shader.
        if (specData.liteMode == 1) {
            specData.enableFilmGrain = 0;
            specData.enableDithering = 0;
            specData.enableRGBEdgeDetection = 0;
            specData.shimmerReduction = 0.0f;
        }

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
        pushConstants.pixelSize.x = texelSizeX;
        pushConstants.pixelSize.y = texelSizeY;

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
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
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CrystalClearPushConstants), &pushConstants);
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

    // Declarative parameter interface
    const std::vector<EffectParamDesc>& CrystalClearEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            // Presets & Performance
            {"crystalclearPreset", "Preset", ParamType::Combo, 0.0, 0.0, 0.0, 0.0,
            {"devfav", "esports", "artifactless", "maxsharp", "vibrantsharp", "devfxaa", "cinematic", "film", "vivid", "noir"},
            "Presets & Performance", -1, 0, 0},
            {"crystalclearLiteMode", "Lite Mode (iGPU)", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Presets & Performance", SPEC(72, liteMode)},

            // Sharpening & Contrast
            {"crystalclearBilateralRadius", "Bilateral Radius", ParamType::Float, 2.5, 0.5, 8.0, 0.1, {}, "Sharpening & Contrast", SPEC(0, radius)},
            {"crystalclearBilateralOffset", "Bilateral Offset", ParamType::Float, 1.5, 0.5, 3.0, 0.1, {}, "Sharpening & Contrast", SPEC(1, offset)},
            {"crystalclearSharpStrength", "Sharp Strength", ParamType::Float, 2.5, 0.0, 5.0, 0.1, {}, "Sharpening & Contrast", SPEC(2, SharpStrength)},
            {"crystalclearBlendMode", "Blend Mode", ParamType::Int, 5.0, 0.0, 6.0, 1.0, {}, "Sharpening & Contrast", SPEC(3, blendMode)},
            {"crystalclearBlendIfDark", "Blend If Dark", ParamType::Int, 8.0, 0.0, 255.0, 1.0, {}, "Sharpening & Contrast", SPEC(4, blendIfDark)},
            {"crystalclearBlendIfLight", "Blend If Light", ParamType::Int, 248.0, 0.0, 255.0, 1.0, {}, "Sharpening & Contrast", SPEC(5, blendIfLight)},
            {"crystalclearCasSharpness", "CAS Sharpness", ParamType::Float, 1.0, 0.0, 1.0, 0.01, {}, "Sharpening & Contrast", SPEC(6, casSharpness)},
            {"crystalclearCasStrength", "CAS Strength", ParamType::Float, 3.0, 0.0, 5.0, 0.1, {}, "Sharpening & Contrast", SPEC(7, casStrength)},
            {"crystalclearLocalContrastStrength", "Local Contrast", ParamType::Float, 0.0, 0.0, 2.0, 0.05, {}, "Sharpening & Contrast", SPEC(41, localContrastStrength)},

            // Anti-Aliasing (FXAA)
            {"crystalclearEnableAA", "Enable AA", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Anti-Aliasing (FXAA)", SPEC(11, enableAA)},
            {"crystalclearFxaaEdgeThreshold", "FXAA Edge Thresh", ParamType::Float, 0.05, 0.001, 1.0, 0.01, {}, "Anti-Aliasing (FXAA)", SPEC(13, fxaaEdgeThreshold)},
            {"crystalclearFxaaEdgeThresholdMin", "FXAA Edge Min", ParamType::Float, 0.0312, 0.0, 1.0, 0.001, {}, "Anti-Aliasing (FXAA)", SPEC(18, fxaaEdgeThresholdMin)},
            {"crystalclearFxaaSubpixAmount", "FXAA Subpix", ParamType::Float, 1.0, 0.0, 1.0, 0.01, {}, "Anti-Aliasing (FXAA)", SPEC(14, fxaaSubpixAmount)},
            {"crystalclearFxaaSearchScale", "FXAA Search Scale", ParamType::Float, 1.0, 0.1, 3.0, 0.1, {}, "Anti-Aliasing (FXAA)", SPEC(15, fxaaSearchScale)},
            {"crystalclearFxaaHardEdgeThreshold", "FXAA Hard Edge", ParamType::Float, 0.08, 0.0, 1.0, 0.01, {}, "Anti-Aliasing (FXAA)", SPEC(16, fxaaHardEdgeThreshold)},
            {"crystalclearFxaaOnlyMode", "FXAA Only", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Anti-Aliasing (FXAA)", SPEC(19, fxaaOnlyMode)},

            // Artifact Protection
            {"crystalclearGuardStrength", "Guard Strength", ParamType::Float, 0.4, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(30, guardStrength)},
            {"crystalclearBandPassWidth", "Band Pass Width", ParamType::Float, 0.85, 0.3, 1.5, 0.05, {}, "Artifact Protection", SPEC(31, bandPassWidth)},
            {"crystalclearExtremeProtection", "Extreme Protection", ParamType::Float, 0.3, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(32, extremeProtection)},
            {"crystalclearShimmerReduction", "Shimmer Reduction", ParamType::Float, 0.4, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(33, shimmerReduction)},
            {"crystalclearEdgeThreshLow", "Edge Thresh Low", ParamType::Float, 0.03, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(8, edgeThreshLow)},
            {"crystalclearEdgeThreshHigh", "Edge Thresh High", ParamType::Float, 0.28, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(9, edgeThreshHigh)},
            {"crystalclearEnableRGBEdgeDetection", "RGB Edge Detection", ParamType::Bool, 1.0, 0.0, 1.0, 1.0, {}, "Artifact Protection", SPEC(12, enableRGBEdgeDetection)},
            {"crystalclearClarityTextureProtection", "Clarity Protection", ParamType::Float, 0.35, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(17, clarityTextureProtection)},
            {"crystalclearEnableChromaSmooth", "Chroma Smooth", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Artifact Protection", SPEC(38, enableChromaSmooth)},
            {"crystalclearChromaSmoothStrength", "Chroma Strength", ParamType::Float, 0.5, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(39, chromaSmoothStrength)},
            {"crystalclearEnableDespeckle", "Enable Despeckle", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Artifact Protection", SPEC(42, enableDespeckle)},
            {"crystalclearDespeckleThreshold", "Despeckle Threshold", ParamType::Float, 0.15, 0.0, 1.0, 0.01, {}, "Artifact Protection", SPEC(43, despeckleThreshold)},
            {"crystalclearEnableFringeFix", "Fringe Fix (CA)", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Artifact Protection", SPEC(44, enableFringeFix)},
            {"crystalclearFringeStrength", "Fringe Strength", ParamType::Float, 0.5, 0.0, 1.0, 0.05, {}, "Artifact Protection", SPEC(45, fringeStrength)},
            {"crystalclearEnableCheckerboardFix", "Checkerboard Fix", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Artifact Protection", SPEC(70, enableCheckerboardFix)},
            {"crystalclearCheckerboardStrength", "Checkerboard Strength", ParamType::Float, 0.5, 0.0, 1.0, 0.05, {}, "Artifact Protection", SPEC(71, checkerboardStrength)},

            // Film Grain & Dither
            {"crystalclearEnableFilmGrain", "Enable Film Grain", ParamType::Bool, 1.0, 0.0, 1.0, 1.0, {}, "Film Grain & Dither", SPEC(23, enableFilmGrain)},
            {"crystalclearFilmGrainStrength", "Grain Strength", ParamType::Float, 1.0, 0.0, 2.0, 0.1, {}, "Film Grain & Dither", SPEC(24, filmGrainStrength)},
            {"crystalclearFilmGrainMinimum", "Grain Minimum", ParamType::Float, 0.0, 0.0, 2.0, 0.1, {}, "Film Grain & Dither", SPEC(25, filmGrainMinimum)},
            {"crystalclearFineGrainWeight", "Fine Grain", ParamType::Float, 0.4, 0.0, 1.0, 0.01, {}, "Film Grain & Dither", SPEC(27, fineGrainWeight)},
            {"crystalclearCoarseGrainWeight", "Coarse Grain", ParamType::Float, 0.8, 0.0, 1.0, 0.01, {}, "Film Grain & Dither", SPEC(28, coarseGrainWeight)},
            {"crystalclearEnableDithering", "Enable Dithering", ParamType::Bool, 1.0, 0.0, 1.0, 1.0, {}, "Film Grain & Dither", SPEC(10, enableDithering)},

            // Color & Tone
            {"crystalclearVibrance", "Vibrance", ParamType::Float, 0.0, -1.0, 1.0, 0.05, {}, "Color & Tone", SPEC(34, vibrance)},
            {"crystalclearSaturation", "Saturation", ParamType::Float, 0.0, -1.0, 1.0, 0.05, {}, "Color & Tone", SPEC(46, saturation)},
            {"crystalclearEnableDeband", "Enable Deband", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Color & Tone", SPEC(35, enableDeband)},
            {"crystalclearDebandStrength", "Deband Strength", ParamType::Float, 0.5, 0.0, 1.0, 0.05, {}, "Color & Tone", SPEC(36, debandStrength)},
            {"crystalclearToneCurve", "Tone Curve", ParamType::Float, 0.0, 0.0, 1.0, 0.01, {}, "Color & Tone", SPEC(37, toneCurve)},
            {"crystalclearSpecularDesat", "Specular Desat", ParamType::Float, 0.0, 0.0, 1.0, 0.01, {}, "Color & Tone", SPEC(40, specularDesat)},

            // Color Grading
            {"crystalclearEnableCDL", "CDL Enable", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Color Grading", SPEC(47, enableCDL)},
            {"crystalclearCDLSlopeR", "CDL Slope R", ParamType::Float, 1.0, 0.0, 4.0, 0.01, {}, "Color Grading", SPEC(48, cdlSlopeR)},
            {"crystalclearCDLSlopeG", "CDL Slope G", ParamType::Float, 1.0, 0.0, 4.0, 0.01, {}, "Color Grading", SPEC(49, cdlSlopeG)},
            {"crystalclearCDLSlopeB", "CDL Slope B", ParamType::Float, 1.0, 0.0, 4.0, 0.01, {}, "Color Grading", SPEC(50, cdlSlopeB)},
            {"crystalclearCDLOffsetR", "CDL Offset R", ParamType::Float, 0.0, -1.0, 1.0, 0.01, {}, "Color Grading", SPEC(51, cdlOffsetR)},
            {"crystalclearCDLOffsetG", "CDL Offset G", ParamType::Float, 0.0, -1.0, 1.0, 0.01, {}, "Color Grading", SPEC(52, cdlOffsetG)},
            {"crystalclearCDLOffsetB", "CDL Offset B", ParamType::Float, 0.0, -1.0, 1.0, 0.01, {}, "Color Grading", SPEC(53, cdlOffsetB)},
            {"crystalclearCDLPowerR", "CDL Power R", ParamType::Float, 1.0, 0.1, 4.0, 0.01, {}, "Color Grading", SPEC(54, cdlPowerR)},
            {"crystalclearCDLPowerG", "CDL Power G", ParamType::Float, 1.0, 0.1, 4.0, 0.01, {}, "Color Grading", SPEC(55, cdlPowerG)},
            {"crystalclearCDLPowerB", "CDL Power B", ParamType::Float, 1.0, 0.1, 4.0, 0.01, {}, "Color Grading", SPEC(56, cdlPowerB)},
            {"crystalclearEnableSplitTone", "Split Tone Enable", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Color Grading", SPEC(57, enableSplitTone)},
            {"crystalclearSTShadowR", "ST Shadow R", ParamType::Float, 0.0, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(58, stShadowR)},
            {"crystalclearSTShadowG", "ST Shadow G", ParamType::Float, 0.5, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(59, stShadowG)},
            {"crystalclearSTShadowB", "ST Shadow B", ParamType::Float, 0.5, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(60, stShadowB)},
            {"crystalclearSTHighR", "ST Highlight R", ParamType::Float, 0.5, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(61, stHighR)},
            {"crystalclearSTHighG", "ST Highlight G", ParamType::Float, 0.3, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(62, stHighG)},
            {"crystalclearSTHighB", "ST Highlight B", ParamType::Float, 0.0, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(63, stHighB)},
            {"crystalclearSplitToneStrength", "Split Tone Strength", ParamType::Float, 0.0, 0.0, 1.0, 0.01, {}, "Color Grading", SPEC(64, splitToneStrength)},
            {"crystalclearTemperature", "Temperature", ParamType::Float, 0.0, -1.0, 1.0, 0.01, {}, "Color Grading", SPEC(65, temperature)},
            {"crystalclearTint", "Tint", ParamType::Float, 0.0, -1.0, 1.0, 0.01, {}, "Color Grading", SPEC(66, tint)},
            {"crystalclearGammaAdjust", "Gamma", ParamType::Float, 0.0, -0.9, 0.9, 0.01, {}, "Color Grading", SPEC(67, gammaAdjust)},
            {"crystalclearBlackLift", "Black Point Lift", ParamType::Float, 0.0, 0.0, 0.5, 0.01, {}, "Color Grading", SPEC(68, blackLift)},
            {"crystalclearWhiteClip", "White Point Clip", ParamType::Float, 0.0, 0.0, 0.5, 0.01, {}, "Color Grading", SPEC(69, whiteClip)},

            // Debug
            {"crystalclearEnableDebugAA", "Debug AA", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Debug", SPEC(20, enableDebugAA)},
            {"crystalclearEnableDebugCAS", "Debug CAS", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Debug", SPEC(21, enableDebugCAS)},
            {"crystalclearEnableDebugClarity", "Debug Clarity", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Debug", SPEC(22, enableDebugClarity)},
            {"crystalclearEnableDebugGrain", "Debug Grain", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "Debug", SPEC(26, enableDebugGrain)},
        };
        return params;
    }

} // namespace vkBasalt
