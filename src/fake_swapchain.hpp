#ifndef FAKE_SWAPCHAIN_HPP_INCLUDED
#define FAKE_SWAPCHAIN_HPP_INCLUDED

#include <vector>

#include "logical_device.hpp"

namespace vkBasalt
{
    std::vector<VkImage> createFakeSwapchainImages(
        LogicalDevice* pLogicalDevice,
        const VkSwapchainCreateInfoKHR& swapchainCreateInfo,
        uint32_t count,
        VkDeviceMemory& deviceMemory,
        const std::vector<VkFormat>& viewFormats);
}

#endif // FAKE_SWAPCHAIN_HPP_INCLUDED
