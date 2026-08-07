#pragma once
#include "effect_simple.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    struct ClarityPushConstants {
        PushVec2 step1;
        PushVec2 step2;
    };

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
        
        ~ClarityEffect();

        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        
        private:
        float radius;
        float offset;
        ClarityPushConstants pushConstants;
    };
} // namespace vkBasalt
