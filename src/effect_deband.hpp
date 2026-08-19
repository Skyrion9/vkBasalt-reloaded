#pragma once

#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"
#include "effect_simple.hpp"
#include "config.hpp"

namespace vkBasalt
{
    class DebandEffect : public SimpleEffect
    {
    public:
        DebandEffect(LogicalDevice*       pLogicalDevice,
                     VkFormat             format,
                     VkExtent2D           imageExtent,
                     std::vector<VkImage> inputImages,
                     std::vector<VkImage> outputImages,
                     Config*              pConfig,
                     VkColorSpaceKHR      colorSpace);
        ~DebandEffect();

        std::string getName() const override { return "deband"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;
    };
} // namespace vkBasalt
