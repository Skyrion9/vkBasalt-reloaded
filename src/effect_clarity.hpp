#pragma once
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"

#include "effect_simple.hpp"
#include "config.hpp"

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
                      Config*              pConfig);
        ~ClarityEffect();
    };
} // namespace vkBasalt