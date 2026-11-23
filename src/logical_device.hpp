#ifndef LOGICAL_DEVICE_HPP_INCLUDED
#define LOGICAL_DEVICE_HPP_INCLUDED
#include <string>
#include <vector>

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"

namespace vkBasalt
{
    struct LogicalDevice
    {
        DeviceDispatch vkd;
        InstanceDispatch vki;
        VkDevice device{};
        VkPhysicalDevice physicalDevice{};
        VkInstance instance{};
        VkQueue queue{};
        uint32_t queueFamilyIndex{};
        VkCommandPool commandPool{};

        // Timestamp query support, cached at device creation. timestampValidBits == 0 means the queue
        // can't write timestamps at all, so every GPU timing path must gate on it.
        // timestampPeriod is nanoseconds per tick, used to convert query deltas into milliseconds.
        uint32_t timestampValidBits = 0;
        float timestampPeriod       = 0.0f;
        bool supportsMutableFormat{};
        bool supportsSwapchainColorspace = false;
        bool supportsHdrMetadata         = false;
        VkFormat swapchainFormat         = VK_FORMAT_UNDEFINED;
        std::vector<VkImage> depthImages;
        std::vector<VkFormat> depthFormats;
        std::vector<VkImageView> depthImageViews;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        std::string pipelineCachePath;
    };
} // namespace vkBasalt

#endif // LOGICAL_DEVICE_HPP_INCLUDED
