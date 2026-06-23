#pragma once

#include "effect_simple.hpp"

namespace vkBasalt
{
    struct ClarityRcasPushConstants {
        PushVec2 step1;
        PushVec2 step2;
    };
    
    class ClarityRcasEffect : public SimpleEffect
    {
    public:
        ClarityRcasEffect(LogicalDevice*       pLogicalDevice,
                          VkFormat             format,
                          VkExtent2D           imageExtent,
                          std::vector<VkImage> inputImages,
                          std::vector<VkImage> outputImages,
                          Config*              pConfig);
        
        ~ClarityRcasEffect();

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        
        // Override to update UBO every frame
        void updateEffect() override;
        
    private:
        float radius;
        float offset;
        ClarityRcasPushConstants pushConstants;
        uint32_t m_frameCounter = 0;
    };
} // namespace vkBasalt
