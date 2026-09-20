#ifndef EFFECT_TRANSFER_HPP_INCLUDED
#define EFFECT_TRANSFER_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"

#include "effect.hpp"
#include "config.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    class TransferEffect : public Effect
    {
    public:
        TransferEffect(
            LogicalDevice* pLogicalDevice,
            VkFormat format,
            VkExtent2D imageExtent,
            std::vector<VkImage> inputImages,
            std::vector<VkImage> outputImages,
            Config* pConfig);
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        ~TransferEffect() override;

    private:
        LogicalDevice* pLogicalDevice;
        std::vector<VkImage> inputImages;
        std::vector<VkImage> outputImages;
        VkExtent2D imageExtent{};
    };
} // namespace vkBasalt
#endif // EFFECT_TRANSFER_HPP_INCLUDED
