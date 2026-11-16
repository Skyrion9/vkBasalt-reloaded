#ifndef FAKE_IMAGE_TRACKER_HPP_INCLUDED
#define FAKE_IMAGE_TRACKER_HPP_INCLUDED

#include <vector>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    struct LogicalDevice;

    // Tracks the actual VkImageUsageFlags of fake/real swapchain images so that sRGB image views can be restricted to a storageless subset, satisfying VUID-VkImageViewCreateInfo-usage-02275.
    // Thread safe: all functions acquire an internal mutex.

    void registerFakeImageUsage(VkImage image, VkImageUsageFlags usage);
    void unregisterFakeImageUsage(VkImage image);
    void unregisterFakeImageUsages(const std::vector<VkImage>& images);

    // Returns true and fills *pRestrictedUsage when an SRGB (otherwise storage incapable) view of a tracked image needs STORAGE_BIT stripped.
    bool getStorageStrippedViewUsage(
        LogicalDevice* pLogicalDevice, VkImage image, VkFormat viewFormat, VkImageUsageFlags* pRestrictedUsage);

} // namespace vkBasalt

#endif // FAKE_IMAGE_TRACKER_HPP_INCLUDED
