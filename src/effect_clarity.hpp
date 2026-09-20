#pragma once
#include "effect_simple.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    class ClarityEffect : public SimpleEffect
    {
    public:
        ClarityEffect(LogicalDevice*       pLogicalDevice,
                    VkFormat             format,
                    VkExtent2D           imageExtent,
                    std::vector<VkImage> inputImages,
                    std::vector<VkImage> outputImages,
                    Config*              pConfig,
                    VkColorSpaceKHR      colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        
        ~ClarityEffect() override;

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

        // Declarative parameter interface
        std::string getName() const override { return "clarity"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;
        
    private:
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
            float step1_x;
            float step1_y;
            float step2_x;
            float step2_y;
            int32_t colorSpaceMode;
        };
    
        float radius;
        float offset;
    };
} // namespace vkBasalt
