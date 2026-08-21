#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "effect_simple.hpp"
#include "vulkan_include.hpp"

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
        int32_t enableBC1Fix;
        float   bc1FixStrength;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(CrystalClearSpecData, field), .specSize = sizeof(((CrystalClearSpecData*)0)->field)

    struct CrystalClearPushConstants {
        PushVec2 step1;
        PushVec2 step2;
        PushVec2 pixelSize;
    };

    using PresetMap = std::unordered_map<std::string, double>;
    const std::unordered_map<std::string, PresetMap>& getPresetTable();

    class CrystalClearEffect : public SimpleEffect
    {
    public:
        CrystalClearEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig,
                           VkColorSpaceKHR      colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        
        ~CrystalClearEffect();

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void updateEffect() override;

        // Declarative parameter interface
        std::string getName() const override { return "crystalclear"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;

        int minQualityForParam(const std::string& key) const override;

    private:
        float radius;
        float offset;
        CrystalClearPushConstants pushConstants;
        uint32_t m_frameCounter = 0;
    };
} // namespace vkBasalt
