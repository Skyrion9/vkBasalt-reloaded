#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <vector>

#include "fake_swapchain.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "memory.hpp"
#include "format.hpp"
#include "vulkan_include.hpp"
#include "fake_image_tracker.hpp"

namespace vkBasalt
{
    // Fixed: Pass swapchainCreateInfo by const reference to prevent struct copying and dangling pointers
    std::vector<VkImage> createFakeSwapchainImages(
        LogicalDevice* pLogicalDevice,
        const VkSwapchainCreateInfoKHR& swapchainCreateInfo,
        uint32_t count,
        VkDeviceMemory& deviceMemory,
        const std::vector<VkFormat>& viewFormats)
    {
        std::vector<VkImage> fakeImages(count);

        VkFormat unormFormat = isSRGB(swapchainCreateInfo.imageFormat) ? convertToUNORM(swapchainCreateInfo.imageFormat)
                                                                       : swapchainCreateInfo.imageFormat;

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
        imageFormatListCreateInfo.pNext           = nullptr;
        imageFormatListCreateInfo.viewFormatCount = static_cast<uint32_t>(viewFormats.size());
        imageFormatListCreateInfo.pViewFormats    = viewFormats.data();

        VkImageCreateInfo imageCreateInfo;
        imageCreateInfo.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext     = viewFormats.size() > 1 ? &imageFormatListCreateInfo : nullptr;
        imageCreateInfo.flags     = viewFormats.size() > 1 ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        // Use UNORM format for fake images. SRGB formats don't support VK_IMAGE_USAGE_STORAGE_BIT, but compute effects like CMAA2 need storage images.
        imageCreateInfo.format                = unormFormat;
        imageCreateInfo.extent.width          = swapchainCreateInfo.imageExtent.width;
        imageCreateInfo.extent.height         = swapchainCreateInfo.imageExtent.height;
        imageCreateInfo.extent.depth          = 1;
        imageCreateInfo.mipLevels             = 1;
        imageCreateInfo.arrayLayers           = swapchainCreateInfo.imageArrayLayers;
        imageCreateInfo.samples               = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling                = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage                 = swapchainCreateInfo.imageUsage | VK_IMAGE_USAGE_SAMPLED_BIT
                                                | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                                | VK_IMAGE_USAGE_STORAGE_BIT; // for compute effects (CMAA2 etc.)
        imageCreateInfo.sharingMode           = swapchainCreateInfo.imageSharingMode;
        imageCreateInfo.queueFamilyIndexCount = swapchainCreateInfo.queueFamilyIndexCount;
        imageCreateInfo.pQueueFamilyIndices   = swapchainCreateInfo.pQueueFamilyIndices;
        imageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result;
        for (uint32_t i = 0; i < count; i++) {
            result =
                pLogicalDevice->vkd.CreateImage(pLogicalDevice->device, &imageCreateInfo, nullptr, &(fakeImages[i]));
            ASSERT_VULKAN(result);
            // Record the usage so SRGB views of this image can be restricted.
            // SRGB has no VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, but the UNORM fake image needs STORAGE for CMAA2 etc.
            registerFakeImageUsage(fakeImages[i], imageCreateInfo.usage);
        }

        // Allocate a bunch of memory for all images at one
        VkMemoryRequirements memoryRequirements;
        pLogicalDevice->vkd.GetImageMemoryRequirements(pLogicalDevice->device, fakeImages[0], &memoryRequirements);

        Logger::debug("fake image size: " + std::to_string(memoryRequirements.size));
        Logger::debug("fake image alignment: " + std::to_string(memoryRequirements.alignment));

        if (memoryRequirements.size % memoryRequirements.alignment != 0) {
            memoryRequirements.size =
                (memoryRequirements.size / memoryRequirements.alignment + 1) * memoryRequirements.alignment;
        }

        VkMemoryAllocateInfo memoryAllocateInfo;
        memoryAllocateInfo.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext          = nullptr;
        memoryAllocateInfo.allocationSize = memoryRequirements.size * count;
        memoryAllocateInfo.memoryTypeIndex =
            findMemoryTypeIndex(pLogicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result =
            pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &memoryAllocateInfo, nullptr, &deviceMemory);
        if (result != VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, fakeImages[i], nullptr);
            fakeImages.clear();
            ASSERT_VULKAN(result);
        }

        for (uint32_t i = 0; i < count; i++) {
            result = pLogicalDevice->vkd.BindImageMemory(
                pLogicalDevice->device, fakeImages[i], deviceMemory, memoryRequirements.size * i);
            if (result != VK_SUCCESS) {
                pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, deviceMemory, nullptr);
                deviceMemory = VK_NULL_HANDLE;
                for (uint32_t j = 0; j < count; j++)
                    pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, fakeImages[j], nullptr);
                fakeImages.clear();
                ASSERT_VULKAN(result);
            }
        }
        return fakeImages;
    }
} // namespace vkBasalt
