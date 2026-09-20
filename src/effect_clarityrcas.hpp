#pragma once

#include "effect_simple.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    class ClarityRcasEffect : public SimpleEffect
    {
    public:
        ClarityRcasEffect(
            LogicalDevice* pLogicalDevice,
            VkFormat format,
            VkExtent2D imageExtent,
            std::vector<VkImage> inputImages,
            std::vector<VkImage> outputImages,
            Config* pConfig,
            VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

        ~ClarityRcasEffect() override;

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

        // Override to update UBO every frame
        void updateEffect() override;

        // Declarative parameter interface
        std::string getName() const override { return "clarityrcas"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;

    private:
        struct ClarityRcasSpecData
        {
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
            float step1_x;
            float step1_y;
            float step2_x;
            float step2_y;
            int32_t colorSpaceMode;
        };

        float radius;
        float offset;
        uint32_t m_frameCounter = 0;
    };
} // namespace vkBasalt
