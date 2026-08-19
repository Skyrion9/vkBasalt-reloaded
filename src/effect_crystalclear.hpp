#pragma once

#include "effect_simple.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    struct CrystalClearPushConstants {
        PushVec2 step1;
        PushVec2 step2;
        PushVec2 pixelSize;
    };

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
        
    private:
        float radius;
        float offset;
        CrystalClearPushConstants pushConstants;
        uint32_t m_frameCounter = 0;
    };
} // namespace vkBasalt
