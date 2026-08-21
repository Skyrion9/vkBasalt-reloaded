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
        float   radius;
        float   offset;
        float   SharpStrength;
        int32_t blendMode;
        int32_t blendIfDark;
        int32_t blendIfLight;
        float   casSharpness;
        float   casStrength;
        float   edgeThreshLow;
        float   edgeThreshHigh;
        int32_t enableDithering;
        int32_t enableAA;
        int32_t enableRGBEdgeDetection;
        float   fxaaEdgeThreshold;
        float   fxaaSubpixAmount;
        float   fxaaSearchScale;
        float   fxaaHardEdgeThreshold;
        float   clarityTextureProtection;
        float   fxaaEdgeThresholdMin;
        int32_t fxaaOnlyMode;
        int32_t enableDebugAA;
        int32_t enableDebugCAS;
        int32_t enableDebugClarity;
        int32_t enableFilmGrain;
        float   filmGrainStrength;
        float   filmGrainMinimum;
        int32_t enableDebugGrain;
        float   fineGrainWeight;
        float   coarseGrainWeight;
        int32_t hdrMode;
        float   guardStrength;
        float   bandPassWidth;
        float   extremeProtection;
        float   shimmerReduction;
        float   vibrance;
        int32_t enableDeband;
        float   debandStrength;
        float   toneCurve;
        int32_t enableChromaSmooth;
        float   chromaSmoothStrength;
        float   specularDesat;
        float   localContrastStrength;
        int32_t enableDespeckle;
        float   despeckleThreshold;
        int32_t enableFringeFix;
        float   fringeStrength;
        float   saturation;
        int32_t enableCDL;
        float   cdlSlopeR;
        float   cdlSlopeG;
        float   cdlSlopeB;
        float   cdlOffsetR;
        float   cdlOffsetG;
        float   cdlOffsetB;
        float   cdlPowerR;
        float   cdlPowerG;
        float   cdlPowerB;
        int32_t enableSplitTone;
        float   stShadowR;
        float   stShadowG;
        float   stShadowB;
        float   stHighR;
        float   stHighG;
        float   stHighB;
        float   splitToneStrength;
        float   temperature;
        float   tint;
        float   gammaAdjust;
        float   blackLift;
        float   whiteClip;
        int32_t enableCheckerboardFix;
        float   checkerboardStrength;
        int32_t qualityLevel;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(CrystalClearSpecData, field), .specSize = sizeof(((CrystalClearSpecData*)0)->field)

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

        // devfav falls back to raw defaults by design
        std::string preset = pConfig->getOption<std::string>("crystalclearPreset", "devfav");
        Logger::debug("CrystalClear Preset: " + preset);
        {
            int idx = 0;
            for (const auto& p : getParamDescs()) {
                if (p.key == "crystalclearPreset") {
                    for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                        if (preset == p.comboOptions[ci]) { idx = (int)ci; break; }
                    }
                    break;
                }
            }
            m_paramValues["crystalclearPreset"] = (double)idx;
        }

        using PresetMap = std::unordered_map<std::string, double>;
        static const std::unordered_map<std::string, PresetMap> presetTable = {

            {"esports", {
                // Sharpening
                {"crystalclearSharpStrength",         2.0},
                {"crystalclearCasStrength",           2.5},
                {"crystalclearLocalContrastStrength", 2.0},
                // Protection
                {"crystalclearQualityLevel",          1},
                {"crystalclearGuardStrength",         0.6},
                {"crystalclearExtremeProtection",     0.5},
                {"crystalclearShimmerReduction",      0.6},
                {"crystalclearEnableDespeckle",       1},
                {"crystalclearDespeckleThreshold",    0.18},
                {"crystalclearEnableChromaSmooth",    1},
                {"crystalclearChromaSmoothStrength",  0.3},
                // Color & Tone
                {"crystalclearVibrance",              0.0},
                {"crystalclearSaturation",            0.1},
                {"crystalclearToneCurve",             0.2},
                // Grain (off for competitive clarity)
                {"crystalclearEnableFilmGrain",       0},
                {"crystalclearFilmGrainStrength",     0.8},
                // Blend range
                {"crystalclearBlendIfDark",           15},
                {"crystalclearBlendIfLight",          240},
            }},

            {"antitaa", {
                // Sharpening (maximum to counter TAA blur)
                {"crystalclearSharpStrength",            3.0},
                {"crystalclearCasStrength",              3.5},
                {"crystalclearCasSharpness",             1.0},
                {"crystalclearLocalContrastStrength",    1.0},
                // Protection (targeted against TAA artifacts)
                {"crystalclearQualityLevel",             0},
                {"crystalclearGuardStrength",            0.6},
                {"crystalclearExtremeProtection",        0.3},
                {"crystalclearShimmerReduction",         0.8},
                {"crystalclearBandPassWidth",            0.9},
                {"crystalclearEdgeThreshLow",            0.02},
                {"crystalclearEdgeThreshHigh",           0.20},
                {"crystalclearClarityTextureProtection", 0.2},
                {"crystalclearEnableRGBEdgeDetection",   1},
                // TAA ghost cleanup suite
                {"crystalclearEnableDespeckle",          1},
                {"crystalclearDespeckleThreshold",       0.12},
                {"crystalclearEnableChromaSmooth",       1},
                {"crystalclearChromaSmoothStrength",     0.5},
                // TAA compression banding
                {"crystalclearEnableDeband",             1},
                {"crystalclearDebandStrength",           0.4},
                {"crystalclearEnableDithering",          1},
                // Color (TAA slightly desaturates)
                {"crystalclearVibrance",                 0.1},
                // Grain off (pure sharpness, grain interferes with deblur)
                {"crystalclearEnableFilmGrain",          0},
            }},

            {"artifactless", {
                // Sharpening (soft)
                {"crystalclearSharpStrength",            1.2},
                {"crystalclearCasStrength",              1.5},
                {"crystalclearLocalContrastStrength",    0.0},
                // Protection (maximum)
                {"crystalclearQualityLevel",             0},
                {"crystalclearGuardStrength",            0.9},
                {"crystalclearExtremeProtection",        0.8},
                {"crystalclearShimmerReduction",         0.8},
                {"crystalclearBandPassWidth",            0.6},
                {"crystalclearEdgeThreshLow",            0.05},
                {"crystalclearEdgeThreshHigh",           0.35},
                {"crystalclearClarityTextureProtection", 0.6},
                // Artifact cleanup suite
                {"crystalclearEnableDespeckle",          1},
                {"crystalclearDespeckleThreshold",       0.15},
                {"crystalclearEnableFringeFix",          1},
                {"crystalclearFringeStrength",           0.4},
                {"crystalclearEnableCheckerboardFix",    1},
                {"crystalclearCheckerboardStrength",     0.4},
                {"crystalclearEnableChromaSmooth",       1},
                {"crystalclearChromaSmoothStrength",     0.4},
                {"crystalclearEnableDeband",             1},
                {"crystalclearDebandStrength",           0.4},
                // Color & Grain
                {"crystalclearSpecularDesat",            0.2},
                {"crystalclearEnableFilmGrain",          0},
            }},

            {"maxsharp", {
                // Sharpening (aggressive)
                {"crystalclearSharpStrength",            3.5},
                {"crystalclearCasStrength",              4.0},
                {"crystalclearCasSharpness",             1.0},
                {"crystalclearLocalContrastStrength",    1.5},
                // Protection (minimal)
                {"crystalclearQualityLevel",             0},
                {"crystalclearGuardStrength",            0.2},
                {"crystalclearExtremeProtection",        0.1},
                {"crystalclearShimmerReduction",         0.2},
                {"crystalclearBandPassWidth",            1.2},
                {"crystalclearEdgeThreshLow",            0.02},
                {"crystalclearEdgeThreshHigh",           0.20},
                {"crystalclearClarityTextureProtection", 0.1},
                {"crystalclearEnableRGBEdgeDetection",   1},
                // Pre-sharpen cleanup
                {"crystalclearEnableDespeckle",          1},
                {"crystalclearDespeckleThreshold",       0.1},
                {"crystalclearEnableChromaSmooth",       1},
                {"crystalclearChromaSmoothStrength",     0.3},
                // Grain
                {"crystalclearFilmGrainStrength",        0.8},
            }},

            {"vibrantsharp", {
                // Sharpening
                {"crystalclearSharpStrength",        2.5},
                {"crystalclearCasStrength",          2.5},
                // Protection
                {"crystalclearQualityLevel",         1},
                {"crystalclearGuardStrength",        0.5},
                {"crystalclearExtremeProtection",    0.4},
                {"crystalclearShimmerReduction",     0.5},
                {"crystalclearEnableDespeckle",      1},
                {"crystalclearDespeckleThreshold",   0.15},
                {"crystalclearEnableChromaSmooth",   1},
                {"crystalclearChromaSmoothStrength", 0.4},
                {"crystalclearEnableDeband",         1},
                {"crystalclearDebandStrength",       0.6},
                // Color & Tone
                {"crystalclearVibrance",             0.6},
                {"crystalclearSpecularDesat",        0.2},
                {"crystalclearTemperature",          0.05},
                {"crystalclearToneCurve",            0.2},
                // Grain
                {"crystalclearFilmGrainStrength",    0.6},
            }},

            {"devfxaa", {
                // Sharpening
                {"crystalclearSharpStrength",         2.0},
                {"crystalclearCasStrength",           2.5},
                // FXAA
                {"crystalclearQualityLevel",          1},
                {"crystalclearEnableAA",              1},
                {"crystalclearFxaaEdgeThreshold",     0.04},
                {"crystalclearFxaaSubpixAmount",      0.8},
                {"crystalclearFxaaSearchScale",       1.5},
                {"crystalclearFxaaHardEdgeThreshold", 0.06},
                // Protection
                {"crystalclearGuardStrength",         0.6},
                {"crystalclearShimmerReduction",      0.5},
                {"crystalclearEnableDespeckle",       1},
                {"crystalclearDespeckleThreshold",    0.15},
                {"crystalclearEnableChromaSmooth",    1},
                {"crystalclearChromaSmoothStrength",  0.3},
                // Grain
                {"crystalclearFilmGrainStrength",     0.8},
            }},

            {"cinematic", {
                // Sharpening (soft)
                {"crystalclearSharpStrength",        1.8},
                {"crystalclearCasStrength",          1.5},
                // Protection
                {"crystalclearQualityLevel",         0},
                {"crystalclearGuardStrength",        0.7},
                {"crystalclearExtremeProtection",    0.6},
                {"crystalclearShimmerReduction",     0.5},
                {"crystalclearBandPassWidth",        0.7},
                // Cleanup
                {"crystalclearEnableDespeckle",      1},
                {"crystalclearDespeckleThreshold",   0.15},
                {"crystalclearEnableChromaSmooth",   1},
                {"crystalclearChromaSmoothStrength", 0.6},
                // Film grain (warm, coarse)
                {"crystalclearFilmGrainStrength",    1.2},
                {"crystalclearFilmGrainMinimum",     0.1},
                {"crystalclearFineGrainWeight",      0.5},
                {"crystalclearCoarseGrainWeight",    0.9},
                // Lens simulation
                {"crystalclearEnableFringeFix",      1},
                {"crystalclearFringeStrength",       0.3},
                {"crystalclearSpecularDesat",        0.4},
                // Tone shaping
                {"crystalclearToneCurve",            0.5},
                {"crystalclearGammaAdjust",         -0.05},
                {"crystalclearBlackLift",            0.08},
                {"crystalclearWhiteClip",            0.05},
                // Color grading (warm shadows, amber highlights)
                {"crystalclearVibrance",            -0.1},
                {"crystalclearTemperature",          0.1},
                {"crystalclearEnableSplitTone",      1},
                {"crystalclearSplitToneStrength",    0.3},
                {"crystalclearSTShadowR",            0.0},
                {"crystalclearSTShadowG",            0.4},
                {"crystalclearSTShadowB",            0.5},
                {"crystalclearSTHighR",              0.5},
                {"crystalclearSTHighG",              0.3},
                {"crystalclearSTHighB",              0.0},
                // Deband & blend range
                {"crystalclearEnableDeband",         1},
                {"crystalclearDebandStrength",       0.5},
                {"crystalclearBlendIfDark",          20},
                {"crystalclearBlendIfLight",         230},
            }},

            {"film", {
                // Sharpening (soft)
                {"crystalclearSharpStrength",        1.5},
                {"crystalclearCasStrength",          1.5},
                // Protection
                {"crystalclearQualityLevel",         0},
                {"crystalclearGuardStrength",        0.7},
                {"crystalclearExtremeProtection",    0.7},
                {"crystalclearShimmerReduction",     0.6},
                // Cleanup
                {"crystalclearEnableDespeckle",      1},
                {"crystalclearDespeckleThreshold",   0.15},
                {"crystalclearEnableChromaSmooth",   1},
                {"crystalclearChromaSmoothStrength", 0.5},
                // Film grain (heavy, textured)
                {"crystalclearFilmGrainStrength",    1.5},
                {"crystalclearFilmGrainMinimum",     0.2},
                {"crystalclearFineGrainWeight",      0.6},
                {"crystalclearCoarseGrainWeight",    0.9},
                // Lens simulation
                {"crystalclearEnableFringeFix",      1},
                {"crystalclearFringeStrength",       0.25},
                {"crystalclearSpecularDesat",        0.3},
                // Tone shaping (faded, muted)
                {"crystalclearToneCurve",            0.4},
                {"crystalclearGammaAdjust",         -0.1},
                {"crystalclearBlackLift",            0.15},
                {"crystalclearWhiteClip",            0.1},
                // Color grading (desaturated, cool shadows)
                {"crystalclearSaturation",          -0.2},
                {"crystalclearTemperature",          0.05},
                {"crystalclearEnableSplitTone",      1},
                {"crystalclearSplitToneStrength",    0.25},
                {"crystalclearSTShadowR",            0.0},
                {"crystalclearSTShadowG",            0.3},
                {"crystalclearSTShadowB",            0.4},
                {"crystalclearSTHighR",              0.4},
                {"crystalclearSTHighG",              0.25},
                {"crystalclearSTHighB",              0.0},
                // Deband
                {"crystalclearEnableDeband",         1},
                {"crystalclearDebandStrength",       0.5},
            }},

            {"vivid", {
                // Sharpening
                {"crystalclearSharpStrength",        2.5},
                {"crystalclearCasStrength",          3.0},
                // Protection
                {"crystalclearQualityLevel",         1},
                {"crystalclearGuardStrength",        0.4},
                {"crystalclearShimmerReduction",     0.5},
                {"crystalclearEnableDespeckle",      1},
                {"crystalclearDespeckleThreshold",   0.15},
                {"crystalclearEnableChromaSmooth",   1},
                {"crystalclearChromaSmoothStrength", 0.3},
                {"crystalclearEnableDeband",         1},
                {"crystalclearDebandStrength",       0.4},
                // Color pop
                {"crystalclearVibrance",             0.3},
                {"crystalclearSaturation",           0.4},
                {"crystalclearTemperature",          0.05},
                {"crystalclearSpecularDesat",        0.2},
                {"crystalclearToneCurve",            0.15},
                // CDL grade (warm reds, lifted greens)
                {"crystalclearEnableCDL",            1},
                {"crystalclearCDLSlopeR",            1.1},
                {"crystalclearCDLSlopeG",            1.05},
                {"crystalclearCDLSlopeB",            1.0},
                // Grain
                {"crystalclearFilmGrainStrength",    0.6},
            }},

            {"noir", {
                // Sharpening (high contrast)
                {"crystalclearSharpStrength",        3.0},
                {"crystalclearCasStrength",          3.5},
                // Protection (minimal)
                {"crystalclearQualityLevel",         1},
                {"crystalclearGuardStrength",        0.3},
                {"crystalclearExtremeProtection",    0.2},
                {"crystalclearShimmerReduction",     0.3},
                // Cleanup
                {"crystalclearEnableDespeckle",      1},
                {"crystalclearDespeckleThreshold",   0.15},
                {"crystalclearEnableChromaSmooth",   1},
                {"crystalclearChromaSmoothStrength", 0.3},
                // B&W conversion
                {"crystalclearSaturation",          -1.0},
                // Tone shaping (crushed, contrasty)
                {"crystalclearToneCurve",            0.3},
                {"crystalclearGammaAdjust",          0.2},
                {"crystalclearBlackLift",            0.05},
                {"crystalclearWhiteClip",            0.15},
                // Grain (visible, filmic)
                {"crystalclearFilmGrainStrength",    1.0},
                {"crystalclearFilmGrainMinimum",     0.3},
                {"crystalclearFineGrainWeight",      0.5},
                {"crystalclearCoarseGrainWeight",    0.7},
                // Banding protection
                {"crystalclearEnableDeband",         1},
                {"crystalclearDebandStrength",       0.5},
                {"crystalclearEnableDithering",      1},
            }},
        };

        auto presetIt = presetTable.find(preset);

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        // Read config, apply presets once, clamp, write to specData by offset, and build mapEntries
        const auto& params = getParamDescs();

        const std::string appliedPresetKey = "crystalclearPresetApplied";
        const std::string lastAppliedPreset = pConfig->getOption<std::string>(appliedPresetKey, "");

        if (preset != lastAppliedPreset) {
            Logger::debug("Applying CrystalClear preset baseline: " + preset);

            for (const auto& p : params) {
                if (p.key == "crystalclearPreset") continue;

                double val = p.defaultVal;
                if (presetIt != presetTable.end()) {
                    auto overrideIt = presetIt->second.find(p.key);
                    if (overrideIt != presetIt->second.end()) {
                        val = std::clamp(overrideIt->second, p.minVal, p.maxVal);
                    }
                }

                if (p.type == ParamType::Combo) {
                    size_t idx = std::min(
                        static_cast<size_t>(val),
                        p.comboOptions.empty() ? 0 : p.comboOptions.size() - 1
                    );

                    if (!p.comboOptions.empty()) {
                        pConfig->setOption(p.key, p.comboOptions[idx]);
                    }
                } else if (p.type == ParamType::Int || p.type == ParamType::Bool) {
                    pConfig->setOption(p.key, std::to_string(static_cast<int32_t>(val)));
                } else {
                    std::string s = std::to_string(val);
                    std::replace(s.begin(), s.end(), ',', '.');
                    pConfig->setOption(p.key, s);
                }
            }

            pConfig->setOption(appliedPresetKey, preset);
        }
    
        CrystalClearSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double def = p.defaultVal;
            double val;

            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = 0;
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) {
                        idx = static_cast<int>(ci);
                        break;
                    }
                }
                val = static_cast<double>(idx);
            } else if (p.type == ParamType::Float) {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(def)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(def)));
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

        specData.hdrMode = isHDR ? 1 : 0;
        mapEntries.push_back({29, offsetof(CrystalClearSpecData, hdrMode), sizeof(int32_t)});

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

    int CrystalClearEffect::minQualityForParam(const std::string& key) const {
        // Parameters disabled below certain quality levels. Returns the MAXIMUM qualityLevel at which the param is still active. If current qualityLevel > returned value, the param is disabled.
        static const std::unordered_map<std::string, int> thresholds = {
            // Perfect only (qualityLevel == 0)
            {"crystalclearEnableRGBEdgeDetection", 0},
            {"crystalclearEnableFringeFix",        0},
            {"crystalclearFringeStrength",         0},
            // Ultra+ (qualityLevel <= 1)
            {"crystalclearLocalContrastStrength",  1},
            // High+ (qualityLevel <= 2)
            {"crystalclearEnableCheckerboardFix",  2},
            {"crystalclearCheckerboardStrength",   2},
            {"crystalclearEnableDespeckle",        2},
            {"crystalclearDespeckleThreshold",     2},
            // Medium+ (qualityLevel <= 3)
            {"crystalclearShimmerReduction",       3},
            {"crystalclearEnableFilmGrain",        3},
            {"crystalclearFilmGrainStrength",      3},
            {"crystalclearFilmGrainMinimum",       3},
            {"crystalclearFineGrainWeight",        3},
            {"crystalclearCoarseGrainWeight",      3},
        };

        auto it = thresholds.find(key);
        if (it != thresholds.end()) return it->second;
        return 4; // Always active at all quality levels
    }

    // Declarative parameter interface
    const std::vector<EffectParamDesc>& CrystalClearEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            // Presets & Performance
            {.key = "crystalclearPreset", .label = "Preset", .type = ParamType::Combo,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.0, .step = 0.0,
             .comboOptions = {"devfav", "esports", "artifactless", "maxsharp", "antitaa", "vibrantsharp", "devfxaa", "cinematic", "film", "vivid", "noir"},
             .category = "Presets & Performance",
             .tooltip = "Curated starting points. Adjust individual sliders after selecting.\n"
                        "devfav: balanced sharpen+grain.\nesports: high clarity, low grain.\n"
                        "antitaa: maximum TAA deblur + ghost cleanup.\n"
                        "artifactless: soft, maximum protection.\nmaxsharp: aggressive sharpen.\n"
                        "vibrantsharp: sharp + color boost.\ndevfxaa: balanced + FXAA.\n"
                        "cinematic: warm tones, split toning, deband.\nfilm: strong grain, muted colors.\n"
                        "vivid: saturated, CDL color grade.\nnoir: high contrast B&W."},

            {.key = "crystalclearQualityLevel", .label = "Quality Level", .type = ParamType::Combo,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 4.0, .step = 1.0,
             .comboOptions = {"Perfect", "Ultra", "High", "Medium", "iGPU"},
             .category = "Presets & Performance",
             .tooltip = "Master switch for feature gating. Grays out disabled params.\n\n"
                        "Perfect: All features. RGB edge, fringe fix, wide-radius fetches, all guards. ~17 tex fetches/pixel.\n"
                        "Ultra: Drops RGB edge detection + fringe fix. Best set-and-forget for discrete GPUs.\n"
                        "High: Also drops wide step2 fetches, local contrast, oiliness/silhouette gates, checkerboard, despeckle. ~13 fetches.\n"
                        "Medium: Also drops shimmer reduction, film grain, saturation/dark-smear guards.\n"
                        "iGPU: Core CAS + Clarity (step1) only. Band pass + edge mask + extreme protection + dithering. Minimum viable quality.",
             SPEC(72, qualityLevel)},

            // Sharpening & Contrast
            {.key = "crystalclearBilateralRadius", .label = "Bilateral Radius", .type = ParamType::Float,
             .defaultVal = 2.5, .minVal = 0.5, .maxVal = 8.0, .step = 0.1,
             .category = "Sharpening & Contrast",
             .tooltip = "Radius of the bilateral macro-contrast kernel. Larger values boost wider features (large-scale contrast). Smaller values focus on fine detail. Default 2.5.",
             SPEC(0, radius)},

            {.key = "crystalclearBilateralOffset", .label = "Bilateral Offset", .type = ParamType::Float,
             .defaultVal = 1.5, .minVal = 0.5, .maxVal = 3.0, .step = 0.1,
             .category = "Sharpening & Contrast",
             .tooltip = "Multiplier on the bilateral sample offset. Combined with Radius to determine actual fetch distance. Higher = wider contrast evaluation. Default 1.5.",
             SPEC(1, offset)},

            {.key = "crystalclearSharpStrength", .label = "Sharp Strength", .type = ParamType::Float,
             .defaultVal = 2.5, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
             .category = "Sharpening & Contrast",
             .tooltip = "Master strength of the bilateral sharpening (Clarity) pass. Controls how strongly the macro-contrast delta is applied to the image. Default 2.5.",
             SPEC(2, SharpStrength)},

            {.key = "crystalclearBlendMode", .label = "Blend Mode", .type = ParamType::Int,
             .defaultVal = 5.0, .minVal = 0.0, .maxVal = 6.0, .step = 1.0,
             .category = "Sharpening & Contrast",
             .tooltip = "How the sharpening delta blends with the original.\n"
                        "0: Soft Light\n1: Overlay\n2: Hard Light\n3: Vivid Light (clamped)\n"
                        "4: Linear Light\n5: Additive (default, best for TAA deblurring)\n6: Simple offset",
             SPEC(3, blendMode)},

            {.key = "crystalclearBlendIfDark", .label = "Blend If Dark", .type = ParamType::Int,
             .defaultVal = 8.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening & Contrast",
             .tooltip = "Photoshop-style 'Blend If' for shadows. Pixels darker than this value receive reduced sharpening. 0 = disabled (sharpen everything). Default 8.",
             SPEC(4, blendIfDark)},

            {.key = "crystalclearBlendIfLight", .label = "Blend If Light", .type = ParamType::Int,
             .defaultVal = 248.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
             .category = "Sharpening & Contrast",
             .tooltip = "Photoshop-style 'Blend If' for highlights. Pixels brighter than this value receive reduced sharpening. 255 = disabled. Default 248.",
             SPEC(5, blendIfLight)},

            {.key = "crystalclearCasSharpness", .label = "CAS Sharpness", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Sharpening & Contrast",
             .tooltip = "AMD FidelityFX CAS sharpness amount (0-1). Controls the peak weight of the CAS micro-detail filter. Higher = more fine detail, more risk of ringing. Default 1.0.",
             SPEC(6, casSharpness)},

            {.key = "crystalclearCasStrength", .label = "CAS Strength", .type = ParamType::Float,
             .defaultVal = 3.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
             .category = "Sharpening & Contrast",
             .tooltip = "Master multiplier on the CAS delta. Amplifies the micro-detail sharpening output. Default 3.0.",
             SPEC(7, casStrength)},

            {.key = "crystalclearLocalContrastStrength", .label = "Local Contrast", .type = ParamType::Float,
             .defaultVal = 2.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.05,
             .category = "Sharpening & Contrast",
             .tooltip = "Wide-radius local contrast boost. Compares pixel to a large-area blur to enhance macro structure. Disabled on High and below (requires step2 wide fetches). Default 2.0 (max).",
             SPEC(41, localContrastStrength)},

            // Anti-Aliasing (FXAA)
            {.key = "crystalclearEnableAA", .label = "Enable AA", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Enables integrated FXAA 3.11 anti-aliasing. Reuses CAS pixel fetches for minimal overhead. Consider SMAA or CMAA for better quality when available. Default off.",
             SPEC(11, enableAA)},

            {.key = "crystalclearFxaaEdgeThreshold", .label = "FXAA Edge Thresh", .type = ParamType::Float,
             .defaultVal = 0.05, .minVal = 0.001, .maxVal = 1.0, .step = 0.01,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Minimum local contrast to trigger FXAA edge detection. Lower = more edges detected (more blur risk). Higher = fewer edges (misses subtle aliasing). Default 0.05.",
             SPEC(13, fxaaEdgeThreshold)},

            {.key = "crystalclearFxaaEdgeThresholdMin", .label = "FXAA Edge Min", .type = ParamType::Float,
             .defaultVal = 0.0312, .minVal = 0.0, .maxVal = 1.0, .step = 0.001,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Absolute minimum edge threshold floor. Prevents FXAA from activating on noise below this level. Default 0.0312 (1/32).",
             SPEC(18, fxaaEdgeThresholdMin)},

            {.key = "crystalclearFxaaSubpixAmount", .label = "FXAA Subpix", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Sub-pixel anti-aliasing amount. Higher = smoother thin lines and diagonal edges, but can blur fine detail. 0 = disable sub-pixel AA. Default 1.0.",
             SPEC(14, fxaaSubpixAmount)},

            {.key = "crystalclearFxaaSearchScale", .label = "FXAA Search Scale", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.1, .maxVal = 3.0, .step = 0.1,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Multiplier on the FXAA edge endpoint search distance. Higher = catches longer diagonal edges, costs more on those edges. Short edges still terminate early. Default 1.0.",
             SPEC(15, fxaaSearchScale)},

            {.key = "crystalclearFxaaHardEdgeThreshold", .label = "FXAA Hard Edge", .type = ParamType::Float,
             .defaultVal = 0.08, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Threshold for 'hard' edge classification. Used internally for sub-pixel thin-line detection. Default 0.08.",
             SPEC(16, fxaaHardEdgeThreshold)},

            {.key = "crystalclearFxaaOnlyMode", .label = "FXAA Only", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Anti-Aliasing (FXAA)",
             .tooltip = "Skip all sharpening/color/grain passes, output only the FXAA result. Useful for debugging or using CrystalClear as a pure AA filter. Default off.",
             SPEC(19, fxaaOnlyMode)},

            // Artifact Protection
            {.key = "crystalclearGuardStrength", .label = "Guard Strength", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Master multiplier for all protective masks (band pass, edge mask, silhouette, saturation, dark smear, texture protection). 0 = all guards bypassed (maximum sharpening, more artifacts). 1 = full protection (conservative). Default 0.5.",
             SPEC(30, guardStrength)},

            {.key = "crystalclearBandPassWidth", .label = "Band Pass Width", .type = ParamType::Float,
             .defaultVal = 0.85, .minVal = 0.3, .maxVal = 1.5, .step = 0.05,
             .category = "Artifact Protection",
             .tooltip = "Width of the band-pass filter on local contrast. Sharpening is strongest on mid-frequency detail and fades on very low (macro structure) and very high (noise) frequencies. Larger = wider band. Default 0.85.",
             SPEC(31, bandPassWidth)},

            {.key = "crystalclearExtremeProtection", .label = "Extreme Protection", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Reduces sharpening at luminance extremes (very dark shadows, very bright highlights) where artifacts are most visible. 0 = no penalty, 1 = full protection. Default 0.4.",
             SPEC(32, extremeProtection)},

            {.key = "crystalclearShimmerReduction", .label = "Shimmer Reduction", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Stabilizes isolated bright/dark pixels that flicker between frames (shimmer). Blends them toward the local cross-average. Disabled on iGPU. Default 0.4.",
             SPEC(33, shimmerReduction)},

            {.key = "crystalclearEdgeThreshLow", .label = "Edge Thresh Low", .type = ParamType::Float,
             .defaultVal = 0.03, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Lower threshold of the bilateral edge smoothstep. Contrast differences below this are fully suppressed. Lower = more fine detail passes through. Default 0.03.",
             SPEC(8, edgeThreshLow)},

            {.key = "crystalclearEdgeThreshHigh", .label = "Edge Thresh High", .type = ParamType::Float,
             .defaultVal = 0.25, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Upper threshold of the bilateral edge smoothstep. Contrast differences above this are fully passed through. The range between Low and High is the smooth transition zone. Default 0.25.",
             SPEC(9, edgeThreshHigh)},

            {.key = "crystalclearEnableRGBEdgeDetection", .label = "RGB Edge Detection", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Artifact Protection",
             .tooltip = "Per-channel edge detection (3x more ALU than luma-only). Catches chroma-only edges that luma misses (e.g., red text on dark green). Perfect quality only. Default on.",
             SPEC(12, enableRGBEdgeDetection)},

            {.key = "crystalclearClarityTextureProtection", .label = "Clarity Protection", .type = ParamType::Float,
             .defaultVal = 0.35, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Reduces CAS sharpening on micro-textures (high directional purity) to prevent amplifying noise and compression artifacts into visible ringing. Higher = more protection. Default 0.35.",
             SPEC(17, clarityTextureProtection)},

            {.key = "crystalclearEnableChromaSmooth", .label = "Chroma Smooth", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Artifact Protection",
             .tooltip = "Edge-aware chroma denoiser. Blurs color channels in flat areas to kill color noise (common from TAA and compression) without softening luma detail. Default on.",
             SPEC(38, enableChromaSmooth)},

            {.key = "crystalclearChromaSmoothStrength", .label = "Chroma Strength", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Strength of the chroma smoothing pass. Higher = more aggressive color noise removal. Default 0.4.",
             SPEC(39, chromaSmoothStrength)},

            {.key = "crystalclearEnableDespeckle", .label = "Enable Despeckle", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Artifact Protection",
             .tooltip = "Removes isolated impulse noise (random bright/dark outlier pixels) that causes the antsy micro-shimmer look. Gated by Despeckle Threshold. Disabled on Medium and below. Default on.",
             SPEC(42, enableDespeckle)},

            {.key = "crystalclearDespeckleThreshold", .label = "Despeckle Threshold", .type = ParamType::Float,
             .defaultVal = 0.15, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Artifact Protection",
             .tooltip = "Minimum isolation (difference from cross-average) to classify a pixel as a speckle. Lower = catches more noise but risks removing real detail. Default 0.15.",
             SPEC(43, despeckleThreshold)},

            {.key = "crystalclearEnableFringeFix", .label = "Fringe Fix (CA)", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Artifact Protection",
             .tooltip = "Chromatic aberration fringe suppression. Detects per-channel edge disagreement and desaturates fringing zones. Requires RGB edge detection. Perfect quality only. Default off.",
             SPEC(44, enableFringeFix)},

            {.key = "crystalclearFringeStrength", .label = "Fringe Strength", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
             .category = "Artifact Protection",
             .tooltip = "Strength of the fringe desaturation. Higher = more aggressive CA removal. Perfect quality only. Default 0.4.",
             SPEC(45, fringeStrength)},

            {.key = "crystalclearEnableCheckerboardFix", .label = "Checkerboard Fix", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Artifact Protection",
             .tooltip = "Corrects checkerboard transparency patterns used by some games for camera obstruction (e.g., foliage). Detects the alternating structure and blends toward the background estimate. Disabled on Medium and below. Default off.",
             SPEC(70, enableCheckerboardFix)},

            {.key = "crystalclearCheckerboardStrength", .label = "Checkerboard Strength", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
             .category = "Artifact Protection",
             .tooltip = "Strength of the checkerboard correction blend. Higher = more aggressive removal. Disabled on Medium and below. Default 0.5.",
             SPEC(71, checkerboardStrength)},

            // Film Grain & Dither
            {.key = "crystalclearEnableFilmGrain", .label = "Enable Film Grain", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Film Grain & Dither",
             .tooltip = "Perceptual film grain with fine + coarse layers. Adds subtle texture that breaks up banding and gives a natural, non-digital look. Weighted by luminance (peaks at mid-gray, fades in shadows/highlights). Disabled on iGPU. Default on.",
             SPEC(23, enableFilmGrain)},

            {.key = "crystalclearFilmGrainStrength", .label = "Grain Strength", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
             .category = "Film Grain & Dither",
             .tooltip = "Master multiplier on grain amplitude. Higher = more visible grain. Disabled on iGPU. Default 1.0.",
             SPEC(24, filmGrainStrength)},

            {.key = "crystalclearFilmGrainMinimum", .label = "Grain Minimum", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
             .category = "Film Grain & Dither",
             .tooltip = "Floor for grain intensity. Ensures a minimum grain presence even in areas where the perceptual mask evaluates low. Higher = grain everywhere. Disabled on iGPU. Default 0.0.",
             SPEC(25, filmGrainMinimum)},

            {.key = "crystalclearFineGrainWeight", .label = "Fine Grain", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Film Grain & Dither",
             .tooltip = "Weight of the fine grain layer (1:1 pixel resolution, updates every frame). Higher = more high-frequency grain texture. Disabled on iGPU. Default 0.4.",
             SPEC(27, fineGrainWeight)},

            {.key = "crystalclearCoarseGrainWeight", .label = "Coarse Grain", .type = ParamType::Float,
             .defaultVal = 0.8, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Film Grain & Dither",
             .tooltip = "Weight of the coarse grain layer (1/4 resolution, updates every 2 frames). Higher = larger, more film-like grain clumps. Disabled on iGPU. Default 0.8.",
             SPEC(28, coarseGrainWeight)},

            {.key = "crystalclearEnableDithering", .label = "Enable Dithering", .type = ParamType::Bool,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Film Grain & Dither",
             .tooltip = "Contrast-adaptive dithering to break up color banding in gradients. Boosted in flat areas where banding is visible, reduced in textured areas. Always available at all quality levels. Default on.",
             SPEC(10, enableDithering)},

            // Color & Tone
            {.key = "crystalclearVibrance", .label = "Vibrance", .type = ParamType::Float,
             .defaultVal = 0.3, .minVal = -1.0, .maxVal = 1.0, .step = 0.05,
             .category = "Color & Tone",
             .tooltip = "Intelligent saturation that boosts muted colors more than already-saturated ones. Positive = more vibrant, negative = desaturate. 0 = off. Default 0.3.",
             SPEC(34, vibrance)},

            {.key = "crystalclearSaturation", .label = "Saturation", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.05,
             .category = "Color & Tone",
             .tooltip = "Uniform saturation adjustment applied to all colors equally. -1 = grayscale, 0 = off, +1 = double saturation. Default 0.0.",
             SPEC(46, saturation)},

            {.key = "crystalclearEnableDeband", .label = "Enable Deband", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Color & Tone",
             .tooltip = "Direction-aware debanding that breaks up color banding in flat/gradient regions by injecting noise. Gated by local contrast and edge mask to avoid affecting real detail. Default off.",
             SPEC(35, enableDeband)},

            {.key = "crystalclearDebandStrength", .label = "Deband Strength", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
             .category = "Color & Tone",
             .tooltip = "Amplitude of the debanding noise. Higher = more aggressive band removal, but can add visible grain to flat areas. Default 0.5.",
             SPEC(36, debandStrength)},

            {.key = "crystalclearToneCurve", .label = "Tone Curve", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color & Tone",
             .tooltip = "Filmic highlight rolloff. Smoothly compresses luminance above the knee point (0.9 SDR, hdrNorm HDR) to prevent harsh clipping. 0 = off. Default 0.0.",
             SPEC(37, toneCurve)},

            {.key = "crystalclearSpecularDesat", .label = "Specular Desat", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color & Tone",
             .tooltip = "Desaturates very bright specular highlights toward white, mimicking real-world bloom where bright reflections lose color. 0 = off, 1 = full desaturation. Default 0.0.",
             SPEC(40, specularDesat)},

            // Color Grading
            {.key = "crystalclearEnableCDL", .label = "CDL Enable", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Color Grading",
             .tooltip = "ASC Color Decision List: per-channel Slope (gain), Offset (lift), and Power (gamma). Standard color grading tool used in film/TV. Default off.",
             SPEC(47, enableCDL)},

            {.key = "crystalclearCDLSlopeR", .label = "CDL Slope R", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Red channel gain multiplier. 1.0 = no change, >1 = boost reds. Default 1.0.",
             SPEC(48, cdlSlopeR)},

            {.key = "crystalclearCDLSlopeG", .label = "CDL Slope G", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Green channel gain multiplier. Default 1.0.",
             SPEC(49, cdlSlopeG)},

            {.key = "crystalclearCDLSlopeB", .label = "CDL Slope B", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Blue channel gain multiplier. Default 1.0.",
             SPEC(50, cdlSlopeB)},

            {.key = "crystalclearCDLOffsetR", .label = "CDL Offset R", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Red channel additive offset (lift). Positive = raise reds, negative = suppress. Default 0.0.",
             SPEC(51, cdlOffsetR)},

            {.key = "crystalclearCDLOffsetG", .label = "CDL Offset G", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Green channel additive offset. Default 0.0.",
             SPEC(52, cdlOffsetG)},

            {.key = "crystalclearCDLOffsetB", .label = "CDL Offset B", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Blue channel additive offset. Default 0.0.",
             SPEC(53, cdlOffsetB)},

            {.key = "crystalclearCDLPowerR", .label = "CDL Power R", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Red channel gamma exponent. <1 = brighten midtones, >1 = darken midtones. Default 1.0.",
             SPEC(54, cdlPowerR)},

            {.key = "crystalclearCDLPowerG", .label = "CDL Power G", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Green channel gamma exponent. Default 1.0.",
             SPEC(55, cdlPowerG)},

            {.key = "crystalclearCDLPowerB", .label = "CDL Power B", .type = ParamType::Float,
             .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Blue channel gamma exponent. Default 1.0.",
             SPEC(56, cdlPowerB)},

            {.key = "crystalclearEnableSplitTone", .label = "Split Tone Enable", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Color Grading",
             .tooltip = "Cinematic split toning: tints shadows and highlights independently. Luminance-preserving (shifts hue without changing brightness). Default off.",
             SPEC(57, enableSplitTone)},

            {.key = "crystalclearSTShadowR", .label = "ST Shadow R", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Shadow tint red channel. Default 0.0.",
             SPEC(58, stShadowR)},

            {.key = "crystalclearSTShadowG", .label = "ST Shadow G", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Shadow tint green channel. Default 0.5.",
             SPEC(59, stShadowG)},

            {.key = "crystalclearSTShadowB", .label = "ST Shadow B", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Shadow tint blue channel. Default 0.5.",
             SPEC(60, stShadowB)},

            {.key = "crystalclearSTHighR", .label = "ST Highlight R", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Highlight tint red channel. Default 0.5.",
             SPEC(61, stHighR)},

            {.key = "crystalclearSTHighG", .label = "ST Highlight G", .type = ParamType::Float,
             .defaultVal = 0.3, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Highlight tint green channel. Default 0.3.",
             SPEC(62, stHighG)},

            {.key = "crystalclearSTHighB", .label = "ST Highlight B", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Highlight tint blue channel. Default 0.0.",
             SPEC(63, stHighB)},

            {.key = "crystalclearSplitToneStrength", .label = "Split Tone Strength", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Master intensity of the split toning effect. 0 = no tint, 1 = full strength. Default 0.0.",
             SPEC(64, splitToneStrength)},

            {.key = "crystalclearTemperature", .label = "Temperature", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "White balance temperature. Positive = warm (amber), negative = cool (blue). Luminance-preserving. Default 0.0 (neutral).",
             SPEC(65, temperature)},

            {.key = "crystalclearTint", .label = "Tint", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "White balance tint along the green-magenta axis. Positive = green, negative = magenta. Luminance-preserving. Default 0.0 (neutral).",
             SPEC(66, tint)},

            {.key = "crystalclearGammaAdjust", .label = "Gamma", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = -0.9, .maxVal = 0.9, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Mid-tone brightness adjustment via gamma curve. Positive = brighter midtones, negative = darker. Does not affect pure black or white. Default 0.0 (off).",
             SPEC(67, gammaAdjust)},

            {.key = "crystalclearBlackLift", .label = "Black Point Lift", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.5, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Raises the black floor for a faded/film look. Linearly lifts dark values toward gray. 0 = off, 0.5 = maximum lift (very washed out). Default 0.0.",
             SPEC(68, blackLift)},

            {.key = "crystalclearWhiteClip", .label = "White Point Clip", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.5, .step = 0.01,
             .category = "Color Grading",
             .tooltip = "Lowers the white ceiling. Clips bright values downward. 0 = off, 0.5 = maximum clip (crushed highlights). Default 0.0.",
             SPEC(69, whiteClip)},

            // Debug
            {.key = "crystalclearEnableDebugAA", .label = "Debug AA", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Debug",
             .tooltip = "Visualizes FXAA activity as a red heat overlay. Brighter red = more anti-aliasing applied to that pixel. Useful for tuning FXAA thresholds. Default off.",
             SPEC(20, enableDebugAA)},

            {.key = "crystalclearEnableDebugCAS", .label = "Debug CAS", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Debug",
             .tooltip = "Visualizes CAS micro-detail sharpening as a green heat overlay. Brighter green = more CAS delta applied. Useful for verifying CAS mask effectiveness. Default off.",
             SPEC(21, enableDebugCAS)},

            {.key = "crystalclearEnableDebugClarity", .label = "Debug Clarity", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Debug",
             .tooltip = "Visualizes bilateral Clarity sharpening as a cyan heat overlay. Brighter cyan = more macro-contrast delta applied. Useful for tuning bilateral radius/thresholds. Default off.",
             SPEC(22, enableDebugClarity)},

            {.key = "crystalclearEnableDebugGrain", .label = "Debug Grain", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Debug",
             .tooltip = "Visualizes film grain intensity as a blue-to-yellow heat overlay. Blue = low grain, yellow = high grain. Useful for tuning grain weights and perceptual mask. Default off.",
             SPEC(26, enableDebugGrain)},
        };
        return params;
    }
} // namespace vkBasalt
