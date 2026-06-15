#pragma once

#include "effect_simple.hpp"

namespace vkBasalt
{
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
        
        // We rely on SimpleEffect::applyEffect() to handle rendering and Push Constants.
    };
} // namespace vkBasalt
