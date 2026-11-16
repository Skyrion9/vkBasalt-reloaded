#include "fake_image_tracker.hpp"
#include "format.hpp"
#include "logical_device.hpp"

#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    static std::mutex g_fakeImageUsageMutex;
    static std::unordered_map<VkImage, VkImageUsageFlags> g_fakeImageUsages;

    void registerFakeImageUsage(VkImage image, VkImageUsageFlags usage)
    {
        if (image == VK_NULL_HANDLE) return;
        std::scoped_lock l(g_fakeImageUsageMutex);
        g_fakeImageUsages[image] = usage;
    }

    void unregisterFakeImageUsage(VkImage image)
    {
        std::scoped_lock l(g_fakeImageUsageMutex);
        g_fakeImageUsages.erase(image);
    }

    void unregisterFakeImageUsages(const std::vector<VkImage>& images)
    {
        std::scoped_lock l(g_fakeImageUsageMutex);
        for (auto img : images) {
            g_fakeImageUsages.erase(img);
        }
    }

    bool getStorageStrippedViewUsage(
        LogicalDevice* pLogicalDevice, VkImage image, VkFormat viewFormat, VkImageUsageFlags* pRestrictedUsage)
    {
        if (!pRestrictedUsage) return false;
        if (supportsStorageImage(pLogicalDevice, viewFormat)) return false;

        std::scoped_lock l(g_fakeImageUsageMutex);
        auto it = g_fakeImageUsages.find(image);
        if (it == g_fakeImageUsages.end()) return false;
        if ((it->second & VK_IMAGE_USAGE_STORAGE_BIT) == 0) return false;

        *pRestrictedUsage = it->second & ~VK_IMAGE_USAGE_STORAGE_BIT;
        return true;
    }

} // namespace vkBasalt
