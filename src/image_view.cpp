#include "image_view.hpp"
#include "logical_device.hpp"
#include "vulkan_include.hpp"
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    // Fixed: Pass vector by const reference to prevent unnecessary heap allocation
    std::vector<VkImageView> createImageViews(LogicalDevice*              pLogicalDevice,
                                              VkFormat                    format,
                                              const std::vector<VkImage>& images,
                                              VkImageViewType             viewType,
                                              VkImageAspectFlags          aspectMask,
                                              uint32_t                    mipLevels)
    {
        std::vector<VkImageView> imageViews(images.size());

        VkImageViewCreateInfo imageViewCreateInfo;

        imageViewCreateInfo.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.pNext        = nullptr;
        imageViewCreateInfo.flags        = 0;
        imageViewCreateInfo.image        = VK_NULL_HANDLE;
        imageViewCreateInfo.viewType     = viewType;
        imageViewCreateInfo.format       = format;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        imageViewCreateInfo.subresourceRange.aspectMask     = aspectMask;
        imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
        imageViewCreateInfo.subresourceRange.levelCount     = mipLevels;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount     = 1;

        for (uint32_t i = 0; i < images.size(); i++)
        {
            imageViewCreateInfo.image = images[i];
            VkResult result           = pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &imageViewCreateInfo, nullptr, &(imageViews[i]));
            if (result != VK_SUCCESS) {
                for (uint32_t j = 0; j < i; j++)
                    pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, imageViews[j], nullptr);
                imageViews.clear();
                ASSERT_VULKAN(result);
            }
        }

        return imageViews;
    }

} // namespace vkBasalt
