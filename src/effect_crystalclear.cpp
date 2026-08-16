#include "effect_crystalclear.hpp"
#include <array>
#include <cstdint>
#include <cmath>
#include <cstddef>
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
        float def_saturation = 0.0f;
        int32_t def_enableSplitTone = 0; float def_splitToneStrength = 0.0f;
        float def_stShadowR = 0.0f; float def_stShadowG = 0.5f; float def_stShadowB = 0.5f;
        float def_stHighR = 0.5f; float def_stHighG = 0.3f; float def_stHighB = 0.0f;
        float def_temperature = 0.0f; float def_tint = 0.0f;
        float def_gammaAdjust = 0.0f; float def_blackLift = 0.0f; float def_whiteClip = 0.0f;
        int32_t def_enableCDL = 0;
        float def_cdlSlopeR = 1.0f; float def_cdlSlopeG = 1.0f; float def_cdlSlopeB = 1.0f;
        float def_cdlPowerR = 1.0f; float def_cdlPowerG = 1.0f; float def_cdlPowerB = 1.0f;

        // Override defaults based on preset via lookup table. -1 means "not set" for [0,+inf) params, -999 for signed params.
        struct PresetOverride {
            float    SharpStrength = -1; float casStrength = -1; float casSharpness = -1;
            float    guardStrength = -1; float extremeProtection = -1; float shimmerReduction = -1;
            float    bandPassWidth = -1; float edgeThreshLow = -1; float edgeThreshHigh = -1;
            float    clarityTextureProtection = -1; int32_t enableAA = -1; float fxaaEdgeThreshold = -1;
            float    fxaaSubpixAmount = -1; float filmGrainStrength = -1; float filmGrainMinimum = -1;
            float    coarseGrainWeight = -1; int32_t enableFilmGrain = -1; int32_t enableDeband = -1;
            float    debandStrength = -1; float toneCurve = -1; float vibrance = -999;
            int32_t  enableChromaSmooth = -1; float chromaSmoothStrength = -1; float specularDesat = -1;
            int32_t  blendIfDark = -1; int32_t blendIfLight = -1;
            float    saturation = -999; int32_t enableSplitTone = -1; float splitToneStrength = -1;
            float    stShadowR = -1; float stShadowG = -1; float stShadowB = -1;
            float    stHighR = -1; float stHighG = -1; float stHighB = -1;
            float    temperature = -999; float tint = -999;
            float    gammaAdjust = -999; float blackLift = -1; float whiteClip = -1;
            int32_t  enableCDL = -1; float cdlSlopeR = -1; float cdlSlopeG = -1; float cdlSlopeB = -1;
            float    cdlPowerR = -1; float cdlPowerG = -1; float cdlPowerB = -1;
        };
        static const std::unordered_map<std::string, PresetOverride> presetTable = {
            {"esports", {.SharpStrength=2.0f, .casStrength=2.5f, .guardStrength=0.6f,
                        .extremeProtection=0.5f, .shimmerReduction=0.6f, .filmGrainStrength=0.8f,
                        .enableFilmGrain=0, .toneCurve=0.2f, .vibrance=0.0f, .blendIfDark=15, .blendIfLight=240}},
            {"artifactless", {.SharpStrength=1.2f, .casStrength=1.5f, .guardStrength=0.9f,
                            .extremeProtection=0.8f, .shimmerReduction=0.8f, .bandPassWidth=0.6f,
                            .edgeThreshLow=0.05f, .edgeThreshHigh=0.35f, .clarityTextureProtection=0.6f,
                            .enableFilmGrain=0, .enableDeband=1, .debandStrength=0.4f,
                            .enableChromaSmooth=1, .chromaSmoothStrength=0.4f, .specularDesat=0.2f}},
            {"maxsharp", {.SharpStrength=3.5f, .casStrength=4.0f, .casSharpness=1.0f,
                        .guardStrength=0.2f, .extremeProtection=0.1f, .shimmerReduction=0.2f,
                        .bandPassWidth=1.2f, .edgeThreshLow=0.02f, .edgeThreshHigh=0.20f,
                        .clarityTextureProtection=0.1f, .filmGrainStrength=0.8f}},
            {"vibrantsharp", {.SharpStrength=2.5f, .casStrength=2.5f, .guardStrength=0.5f,
                            .extremeProtection=0.4f, .shimmerReduction=0.5f, .filmGrainStrength=0.6f,
                            .enableDeband=1, .debandStrength=0.6f, .toneCurve=0.2f, .vibrance=0.6f,
                            .specularDesat=0.1f}},
            {"devfxaa", {.SharpStrength=2.0f, .casStrength=2.5f, .guardStrength=0.6f,
                        .enableAA=1, .fxaaEdgeThreshold=0.04f, .fxaaSubpixAmount=0.8f,
                        .filmGrainStrength=0.8f}},
            {"cinematic", {.SharpStrength=1.8f, .casStrength=1.5f, .guardStrength=0.7f,
                        .extremeProtection=0.6f, .shimmerReduction=0.5f, .bandPassWidth=0.7f,
                        .filmGrainStrength=1.2f, .filmGrainMinimum=0.1f, .coarseGrainWeight=0.9f,
                        .enableDeband=1, .debandStrength=0.5f, .toneCurve=0.5f, .vibrance=-0.1f,
                        .enableChromaSmooth=1, .chromaSmoothStrength=0.6f, .specularDesat=0.4f,
                        .blendIfDark=20, .blendIfLight=230,
                        .enableSplitTone=1, .splitToneStrength=0.3f,
                        .stShadowR=0.0f, .stShadowG=0.4f, .stShadowB=0.5f,
                        .stHighR=0.5f, .stHighG=0.3f, .stHighB=0.0f,
                        .temperature=0.1f}},
            {"film", {.SharpStrength=1.5f, .casStrength=1.5f, .guardStrength=0.7f,
                    .extremeProtection=0.7f, .shimmerReduction=0.6f,
                    .filmGrainStrength=1.5f, .filmGrainMinimum=0.2f, .coarseGrainWeight=0.9f,
                    .enableDeband=1, .debandStrength=0.5f, .toneCurve=0.4f,
                    .saturation=-0.2f, .enableSplitTone=1, .splitToneStrength=0.25f,
                    .stShadowR=0.0f, .stShadowG=0.3f, .stShadowB=0.4f,
                    .stHighR=0.4f, .stHighG=0.25f, .stHighB=0.0f,
                    .temperature=0.05f, .gammaAdjust=-0.1f, .blackLift=0.15f, .whiteClip=0.1f}},
            {"vivid", {.SharpStrength=2.5f, .casStrength=3.0f, .guardStrength=0.4f,
                    .filmGrainStrength=0.6f, .vibrance=0.3f, .saturation=0.4f,
                    .temperature=0.05f, .enableCDL=1,
                    .cdlSlopeR=1.1f, .cdlSlopeG=1.05f, .cdlSlopeB=1.0f}},
            {"noir", {.SharpStrength=3.0f, .casStrength=3.5f, .guardStrength=0.3f,
                    .extremeProtection=0.2f, .shimmerReduction=0.3f,
                    .filmGrainStrength=1.0f, .toneCurve=0.3f,
                    .saturation=-1.0f, .gammaAdjust=0.2f, .blackLift=0.05f}},
        };

        auto presetIt = presetTable.find(preset);
        if (presetIt != presetTable.end()) {
            const auto& p = presetIt->second;
            if (p.SharpStrength >= 0)          def_SharpStrength = p.SharpStrength;
            if (p.casStrength >= 0)            def_casStrength = p.casStrength;
            if (p.casSharpness >= 0)           def_casSharpness = p.casSharpness;
            if (p.guardStrength >= 0)          def_guardStrength = p.guardStrength;
            if (p.extremeProtection >= 0)      def_extremeProtection = p.extremeProtection;
            if (p.shimmerReduction >= 0)       def_shimmerReduction = p.shimmerReduction;
            if (p.bandPassWidth >= 0)          def_bandPassWidth = p.bandPassWidth;
            if (p.edgeThreshLow >= 0)          def_edgeThreshLow = p.edgeThreshLow;
            if (p.edgeThreshHigh >= 0)         def_edgeThreshHigh = p.edgeThreshHigh;
            if (p.clarityTextureProtection >= 0) def_clarityTextureProtection = p.clarityTextureProtection;
            if (p.enableAA >= 0)               def_enableAA = p.enableAA;
            if (p.fxaaEdgeThreshold >= 0)      def_fxaaEdgeThreshold = p.fxaaEdgeThreshold;
            if (p.fxaaSubpixAmount >= 0)       def_fxaaSubpixAmount = p.fxaaSubpixAmount;
            if (p.filmGrainStrength >= 0)      def_filmGrainStrength = p.filmGrainStrength;
            if (p.filmGrainMinimum >= 0)       def_filmGrainMinimum = p.filmGrainMinimum;
            if (p.coarseGrainWeight >= 0)      def_coarseGrainWeight = p.coarseGrainWeight;
            if (p.enableFilmGrain >= 0)        def_enableFilmGrain = p.enableFilmGrain;
            if (p.enableDeband >= 0)           def_enableDeband = p.enableDeband;
            if (p.debandStrength >= 0)         def_debandStrength = p.debandStrength;
            if (p.toneCurve >= 0)              def_toneCurve = p.toneCurve;
            if (p.vibrance > -999)             def_vibrance = p.vibrance;
            if (p.enableChromaSmooth >= 0)     def_enableChromaSmooth = p.enableChromaSmooth;
            if (p.chromaSmoothStrength >= 0)   def_chromaSmoothStrength = p.chromaSmoothStrength;
            if (p.specularDesat >= 0)          def_specularDesat = p.specularDesat;
            if (p.blendIfDark >= 0)            def_blendIfDark = p.blendIfDark;
            if (p.blendIfLight >= 0)           def_blendIfLight = p.blendIfLight;
            if (p.saturation > -999)           def_saturation = p.saturation;
            if (p.enableSplitTone >= 0)        def_enableSplitTone = p.enableSplitTone;
            if (p.splitToneStrength >= 0)      def_splitToneStrength = p.splitToneStrength;
            if (p.stShadowR >= 0)              def_stShadowR = p.stShadowR;
            if (p.stShadowG >= 0)              def_stShadowG = p.stShadowG;
            if (p.stShadowB >= 0)              def_stShadowB = p.stShadowB;
            if (p.stHighR >= 0)                def_stHighR = p.stHighR;
            if (p.stHighG >= 0)                def_stHighG = p.stHighG;
            if (p.stHighB >= 0)                def_stHighB = p.stHighB;
            if (p.temperature > -999)          def_temperature = p.temperature;
            if (p.tint > -999)                 def_tint = p.tint;
            if (p.gammaAdjust > -999)          def_gammaAdjust = p.gammaAdjust;
            if (p.blackLift >= 0)              def_blackLift = p.blackLift;
            if (p.whiteClip >= 0)              def_whiteClip = p.whiteClip;
            if (p.enableCDL >= 0)              def_enableCDL = p.enableCDL;
            if (p.cdlSlopeR >= 0)              def_cdlSlopeR = p.cdlSlopeR;
            if (p.cdlSlopeG >= 0)              def_cdlSlopeG = p.cdlSlopeG;
            if (p.cdlSlopeB >= 0)              def_cdlSlopeB = p.cdlSlopeB;
            if (p.cdlPowerR >= 0)              def_cdlPowerR = p.cdlPowerR;
            if (p.cdlPowerG >= 0)              def_cdlPowerG = p.cdlPowerG;
            if (p.cdlPowerB >= 0)              def_cdlPowerB = p.cdlPowerB;
        }

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
            float specularDesat; float localContrastStrength; int32_t enableDespeckle; float despeckleThreshold;
            int32_t enableFringeFix; float fringeStrength; float saturation;
            int32_t enableCDL;
            float cdlSlopeR; float cdlSlopeG; float cdlSlopeB;
            float cdlOffsetR; float cdlOffsetG; float cdlOffsetB;
            float cdlPowerR; float cdlPowerG; float cdlPowerB;
            int32_t enableSplitTone;
            float stShadowR; float stShadowG; float stShadowB;
            float stHighR; float stHighG; float stHighB;
            float splitToneStrength; float temperature; float tint;
            float gammaAdjust; float blackLift; float whiteClip;
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
        specData.localContrastStrength     = std::clamp((float)getAndStore("crystalclearLocalContrastStrength", 0.0f), 0.0f, 2.0f);
        specData.enableDespeckle           = std::clamp((int32_t)getAndStoreInt("crystalclearEnableDespeckle", 0), int32_t(0), int32_t(1));
        specData.despeckleThreshold        = std::clamp((float)getAndStore("crystalclearDespeckleThreshold", 0.15f), 0.0f, 1.0f);
        specData.enableFringeFix           = std::clamp((int32_t)getAndStoreInt("crystalclearEnableFringeFix", 0), int32_t(0), int32_t(1));
        specData.fringeStrength            = std::clamp((float)getAndStore("crystalclearFringeStrength", 0.5f), 0.0f, 1.0f);
        specData.saturation                = std::clamp((float)getAndStore("crystalclearSaturation", def_saturation), -1.0f, 1.0f);
        specData.enableCDL                 = std::clamp((int32_t)getAndStoreInt("crystalclearEnableCDL", def_enableCDL), int32_t(0), int32_t(1));
        specData.cdlSlopeR                 = std::clamp((float)getAndStore("crystalclearCDLSlopeR", def_cdlSlopeR), 0.0f, 4.0f);
        specData.cdlSlopeG                 = std::clamp((float)getAndStore("crystalclearCDLSlopeG", def_cdlSlopeG), 0.0f, 4.0f);
        specData.cdlSlopeB                 = std::clamp((float)getAndStore("crystalclearCDLSlopeB", def_cdlSlopeB), 0.0f, 4.0f);
        specData.cdlOffsetR                = std::clamp((float)getAndStore("crystalclearCDLOffsetR", 0.0f), -1.0f, 1.0f);
        specData.cdlOffsetG                = std::clamp((float)getAndStore("crystalclearCDLOffsetG", 0.0f), -1.0f, 1.0f);
        specData.cdlOffsetB                = std::clamp((float)getAndStore("crystalclearCDLOffsetB", 0.0f), -1.0f, 1.0f);
        specData.cdlPowerR                 = std::clamp((float)getAndStore("crystalclearCDLPowerR", def_cdlPowerR), 0.1f, 4.0f);
        specData.cdlPowerG                 = std::clamp((float)getAndStore("crystalclearCDLPowerG", def_cdlPowerG), 0.1f, 4.0f);
        specData.cdlPowerB                 = std::clamp((float)getAndStore("crystalclearCDLPowerB", def_cdlPowerB), 0.1f, 4.0f);
        specData.enableSplitTone           = std::clamp((int32_t)getAndStoreInt("crystalclearEnableSplitTone", def_enableSplitTone), int32_t(0), int32_t(1));
        specData.stShadowR                 = std::clamp((float)getAndStore("crystalclearSTShadowR", def_stShadowR), 0.0f, 1.0f);
        specData.stShadowG                 = std::clamp((float)getAndStore("crystalclearSTShadowG", def_stShadowG), 0.0f, 1.0f);
        specData.stShadowB                 = std::clamp((float)getAndStore("crystalclearSTShadowB", def_stShadowB), 0.0f, 1.0f);
        specData.stHighR                   = std::clamp((float)getAndStore("crystalclearSTHighR", def_stHighR), 0.0f, 1.0f);
        specData.stHighG                   = std::clamp((float)getAndStore("crystalclearSTHighG", def_stHighG), 0.0f, 1.0f);
        specData.stHighB                   = std::clamp((float)getAndStore("crystalclearSTHighB", def_stHighB), 0.0f, 1.0f);
        specData.splitToneStrength         = std::clamp((float)getAndStore("crystalclearSplitToneStrength", def_splitToneStrength), 0.0f, 1.0f);
        specData.temperature               = std::clamp((float)getAndStore("crystalclearTemperature", def_temperature), -1.0f, 1.0f);
        specData.tint                      = std::clamp((float)getAndStore("crystalclearTint", def_tint), -1.0f, 1.0f);
        specData.gammaAdjust               = std::clamp((float)getAndStore("crystalclearGammaAdjust", def_gammaAdjust), -0.9f, 0.9f);
        specData.blackLift                 = std::clamp((float)getAndStore("crystalclearBlackLift", def_blackLift), 0.0f, 0.5f);
        specData.whiteClip                 = std::clamp((float)getAndStore("crystalclearWhiteClip", def_whiteClip), 0.0f, 0.5f);

        VkSpecializationMapEntry mapEntries[] = {
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
            {40, offsetof(CrystalClearSpecData, specularDesat),             sizeof(float)},
            {41, offsetof(CrystalClearSpecData, localContrastStrength),     sizeof(float)},
            {42, offsetof(CrystalClearSpecData, enableDespeckle),           sizeof(int32_t)},
            {43, offsetof(CrystalClearSpecData, despeckleThreshold),        sizeof(float)},
            {44, offsetof(CrystalClearSpecData, enableFringeFix),           sizeof(int32_t)},
            {45, offsetof(CrystalClearSpecData, fringeStrength),            sizeof(float)},
            {46, offsetof(CrystalClearSpecData, saturation),                sizeof(float)},
            {47, offsetof(CrystalClearSpecData, enableCDL),                 sizeof(int32_t)},
            {48, offsetof(CrystalClearSpecData, cdlSlopeR),                 sizeof(float)},
            {49, offsetof(CrystalClearSpecData, cdlSlopeG),                 sizeof(float)},
            {50, offsetof(CrystalClearSpecData, cdlSlopeB),                 sizeof(float)},
            {51, offsetof(CrystalClearSpecData, cdlOffsetR),                sizeof(float)},
            {52, offsetof(CrystalClearSpecData, cdlOffsetG),                sizeof(float)},
            {53, offsetof(CrystalClearSpecData, cdlOffsetB),                sizeof(float)},
            {54, offsetof(CrystalClearSpecData, cdlPowerR),                 sizeof(float)},
            {55, offsetof(CrystalClearSpecData, cdlPowerG),                 sizeof(float)},
            {56, offsetof(CrystalClearSpecData, cdlPowerB),                 sizeof(float)},
            {57, offsetof(CrystalClearSpecData, enableSplitTone),           sizeof(int32_t)},
            {58, offsetof(CrystalClearSpecData, stShadowR),                 sizeof(float)},
            {59, offsetof(CrystalClearSpecData, stShadowG),                 sizeof(float)},
            {60, offsetof(CrystalClearSpecData, stShadowB),                 sizeof(float)},
            {61, offsetof(CrystalClearSpecData, stHighR),                   sizeof(float)},
            {62, offsetof(CrystalClearSpecData, stHighG),                   sizeof(float)},
            {63, offsetof(CrystalClearSpecData, stHighB),                   sizeof(float)},
            {64, offsetof(CrystalClearSpecData, splitToneStrength),         sizeof(float)},
            {65, offsetof(CrystalClearSpecData, temperature),               sizeof(float)},
            {66, offsetof(CrystalClearSpecData, tint),                      sizeof(float)},
            {67, offsetof(CrystalClearSpecData, gammaAdjust),               sizeof(float)},
            {68, offsetof(CrystalClearSpecData, blackLift),                 sizeof(float)},
            {69, offsetof(CrystalClearSpecData, whiteClip),                 sizeof(float)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = sizeof(mapEntries) / sizeof(mapEntries[0]);
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

        // BOTTOM_OF_PIPE is sufficient because no subsequent pipeline work reads inputImages.
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &secondBarrier);

        // Barrier 3: Prepare outputImages for the next consumer. The render pass left outputImages in PRESENT_SRC_KHR (finalLayout).  If we are an intermediate effect, the next effect expects SHADER_READ_ONLY_OPTIMAL.
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
            {"crystalclearPreset", "Preset", ParamType::Combo, 0.0, 0.0, 0.0, 0.0,
            {"devfav", "esports", "artifactless", "maxsharp", "vibrantsharp", "devfxaa", "cinematic", "film", "vivid", "noir"}, "Preset"},
            {"crystalclearBilateralRadius",       "Bilateral Radius",       ParamType::Float, 2.5,   0.5,   8.0,   0.1, {}, "Sharpening"},
            {"crystalclearBilateralOffset",       "Bilateral Offset",       ParamType::Float, 1.5,   0.5,   3.0,   0.1, {}, "Sharpening"},
            {"crystalclearSharpStrength",         "Sharp Strength",         ParamType::Float, 2.5,   0.0,   5.0,   0.1, {}, "Sharpening"},
            {"crystalclearBlendMode",             "Blend Mode",             ParamType::Int,   5.0,   0.0,   6.0,   1.0, {}, "Sharpening"},
            {"crystalclearBlendIfDark",           "Blend If Dark",          ParamType::Int,   8.0,   0.0, 255.0,   1.0, {}, "Sharpening"},
            {"crystalclearBlendIfLight",          "Blend If Light",         ParamType::Int, 248.0,   0.0, 255.0,   1.0, {}, "Sharpening"},
            {"crystalclearCasSharpness",          "CAS Sharpness",          ParamType::Float, 1.0,   0.0,   1.0,  0.01, {}, "Sharpening"},
            {"crystalclearCasStrength",           "CAS Strength",           ParamType::Float, 3.0,   0.0,   5.0,   0.1, {}, "Sharpening"},
            {"crystalclearEdgeThreshLow",         "Edge Thresh Low",        ParamType::Float, 0.03,  0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearEdgeThreshHigh",        "Edge Thresh High",       ParamType::Float, 0.28,  0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearEnableDithering",       "Enable Dithering",       ParamType::Bool,  1.0,   0.0,   1.0,   1.0, {}, "Grain & Dither"},
            {"crystalclearEnableAA",              "Enable AA",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Anti-Aliasing"},
            {"crystalclearEnableRGBEdgeDetection","RGB Edge Detection",     ParamType::Bool,  1.0,   0.0,   1.0,   1.0, {}, "Protection"},
            {"crystalclearFxaaEdgeThreshold",     "FXAA Edge Thresh",       ParamType::Float, 0.05,  0.001, 1.0,  0.01, {}, "Anti-Aliasing"},
            {"crystalclearFxaaSubpixAmount",      "FXAA Subpix",            ParamType::Float, 1.0,   0.0,   1.0,  0.01, {}, "Anti-Aliasing"},
            {"crystalclearFxaaSearchScale",       "FXAA Search Scale",      ParamType::Float, 1.0,   0.1,   3.0,   0.1, {}, "Anti-Aliasing"},
            {"crystalclearFxaaHardEdgeThreshold", "FXAA Hard Edge",         ParamType::Float, 0.08,  0.0,   1.0,  0.01, {}, "Anti-Aliasing"},
            {"crystalclearClarityTextureProtection","Clarity Protection",   ParamType::Float, 0.35,  0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearFxaaEdgeThresholdMin",  "FXAA Edge Min",          ParamType::Float, 0.0312,0.0,   1.0, 0.001, {}, "Anti-Aliasing"},
            {"crystalclearFxaaOnlyMode",          "FXAA Only",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Anti-Aliasing"},
            {"crystalclearEnableDebugAA",         "Debug AA",               ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Debug"},
            {"crystalclearEnableDebugCAS",        "Debug CAS",              ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Debug"},
            {"crystalclearEnableDebugClarity",    "Debug Clarity",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Debug"},
            {"crystalclearEnableFilmGrain",       "Enable Film Grain",      ParamType::Bool,  1.0,   0.0,   1.0,   1.0, {}, "Grain & Dither"},
            {"crystalclearFilmGrainStrength",     "Grain Strength",         ParamType::Float, 1.0,   0.0,   2.0,   0.1, {}, "Grain & Dither"},
            {"crystalclearFilmGrainMinimum",      "Grain Minimum",          ParamType::Float, 0.0,   0.0,   2.0,   0.1, {}, "Grain & Dither"},
            {"crystalclearEnableDebugGrain",      "Debug Grain",            ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Debug"},
            {"crystalclearFineGrainWeight",       "Fine Grain",             ParamType::Float, 0.4,   0.0,   1.0,  0.01, {}, "Grain & Dither"},
            {"crystalclearCoarseGrainWeight",     "Coarse Grain",           ParamType::Float, 0.8,   0.0,   1.0,  0.01, {}, "Grain & Dither"},
            {"crystalclearGuardStrength",         "Guard Strength",         ParamType::Float, 0.4,   0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearBandPassWidth",         "Band Pass Width",        ParamType::Float, 0.85,  0.3,   1.5,  0.05, {}, "Protection"},
            {"crystalclearExtremeProtection",     "Extreme Protection",     ParamType::Float, 0.3,   0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearShimmerReduction",      "Shimmer Reduction",      ParamType::Float, 0.4,   0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearVibrance",              "Vibrance",               ParamType::Float, 0.0,  -1.0,   1.0,  0.05, {}, "Color & Tone"},
            {"crystalclearEnableDeband",          "Enable Deband",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Color & Tone"},
            {"crystalclearDebandStrength",        "Deband Strength",        ParamType::Float, 0.5,   0.0,   1.0,  0.05, {}, "Color & Tone"},
            {"crystalclearToneCurve",             "Tone Curve",             ParamType::Float, 0.0,   0.0,   1.0,  0.01, {}, "Color & Tone"},
            {"crystalclearEnableChromaSmooth",    "Chroma Smooth",          ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Protection"},
            {"crystalclearChromaSmoothStrength",  "Chroma Strength",        ParamType::Float, 0.5,   0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearSpecularDesat",         "Specular Desat",         ParamType::Float, 0.0,   0.0,   1.0,  0.01, {}, "Color & Tone"},
            {"crystalclearLocalContrastStrength", "Local Contrast",         ParamType::Float, 0.0,   0.0,   2.0,  0.05, {}, "Sharpening"},
            {"crystalclearEnableDespeckle",       "Enable Despeckle",       ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Protection"},
            {"crystalclearDespeckleThreshold",    "Despeckle Threshold",    ParamType::Float, 0.15,  0.0,   1.0,  0.01, {}, "Protection"},
            {"crystalclearEnableFringeFix",       "Fringe Fix (CA)",        ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Protection"},
            {"crystalclearFringeStrength",        "Fringe Strength",        ParamType::Float, 0.5,   0.0,   1.0,  0.05, {}, "Protection"},
            {"crystalclearSaturation",            "Saturation",             ParamType::Float, 0.0,  -1.0,   1.0,  0.05, {}, "Color Grade"},
            {"crystalclearEnableCDL",             "CDL Enable",             ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Color Grade"},
            {"crystalclearCDLSlopeR",             "CDL Slope R",            ParamType::Float, 1.0,   0.0,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLSlopeG",             "CDL Slope G",            ParamType::Float, 1.0,   0.0,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLSlopeB",             "CDL Slope B",            ParamType::Float, 1.0,   0.0,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLOffsetR",            "CDL Offset R",           ParamType::Float, 0.0,  -1.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLOffsetG",            "CDL Offset G",           ParamType::Float, 0.0,  -1.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLOffsetB",            "CDL Offset B",           ParamType::Float, 0.0,  -1.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLPowerR",             "CDL Power R",            ParamType::Float, 1.0,   0.1,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLPowerG",             "CDL Power G",            ParamType::Float, 1.0,   0.1,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearCDLPowerB",             "CDL Power B",            ParamType::Float, 1.0,   0.1,   4.0,  0.01, {}, "Color Grade"},
            {"crystalclearEnableSplitTone",       "Split Tone Enable",      ParamType::Bool,  0.0,   0.0,   1.0,   1.0, {}, "Color Grade"},
            {"crystalclearSTShadowR",             "ST Shadow R",            ParamType::Float, 0.0,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSTShadowG",             "ST Shadow G",            ParamType::Float, 0.5,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSTShadowB",             "ST Shadow B",            ParamType::Float, 0.5,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSTHighR",               "ST Highlight R",         ParamType::Float, 0.5,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSTHighG",               "ST Highlight G",         ParamType::Float, 0.3,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSTHighB",               "ST Highlight B",         ParamType::Float, 0.0,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearSplitToneStrength",     "Split Tone Strength",    ParamType::Float, 0.0,   0.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearTemperature",           "Temperature",            ParamType::Float, 0.0,  -1.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearTint",                  "Tint",                   ParamType::Float, 0.0,  -1.0,   1.0,  0.01, {}, "Color Grade"},
            {"crystalclearGammaAdjust",           "Gamma",                  ParamType::Float, 0.0,  -0.9,   0.9,  0.01, {}, "Color Grade"},
            {"crystalclearBlackLift",             "Black Point Lift",       ParamType::Float, 0.0,   0.0,   0.5,  0.01, {}, "Color Grade"},
            {"crystalclearWhiteClip",             "White Point Clip",       ParamType::Float, 0.0,   0.0,   0.5,  0.01, {}, "Color Grade"},
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
