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
    class CasEffect : public SimpleEffect
    {
    public:
        CasEffect(LogicalDevice*       pLogicalDevice,
                  VkFormat             format,
                  VkExtent2D           imageExtent,
                  std::vector<VkImage> inputImages,
                  std::vector<VkImage> outputImages,
                  Config*              pConfig,
                  VkColorSpaceKHR      colorSpace);
        ~CasEffect();

        std::string getName() const override { return "cas"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;

    private:
        struct CasSpecData {
            float sharpness;
            float contrastLimit;
            int32_t colorSpaceMode;
        };
    };
} // namespace vkBasalt
