#pragma once

#include "effect_simple.hpp"

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
                           Config*              pConfig);
        
        ~CrystalClearEffect();

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        
        // Override to update UBO every frame
        void updateEffect() override;
        
    private:
        float radius;
        float offset;
        CrystalClearPushConstants pushConstants;
        uint32_t m_frameCounter = 0;
    };
} // namespace vkBasalt
