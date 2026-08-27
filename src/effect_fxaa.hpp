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
    class FxaaEffect : public SimpleEffect
    {
    public:
        FxaaEffect(LogicalDevice*       pLogicalDevice,
                   VkFormat             format,
                   VkExtent2D           imageExtent,
                   std::vector<VkImage> inputImages,
                   std::vector<VkImage> outputImages,
                   Config*              pConfig,
                   VkColorSpaceKHR      colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        ~FxaaEffect();

        std::string getName() const override { return "fxaa"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;
    };
} // namespace vkBasalt
