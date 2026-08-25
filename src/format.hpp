#ifndef FORMAT_HPP_INCLUDED
#define FORMAT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    // Returns a matching sRGB format to a UNORM format if it exist, else returns format
    VkFormat convertToSRGB(VkFormat format);
    // Returns a matching UNORM format to a sRGB format if it exist, else returns format
    VkFormat convertToUNORM(VkFormat format);

    uint32_t getBytesPerPixel(VkFormat format);
    float halfToFloat(uint16_t h);


    VkFormat getSupportedFormat(LogicalDevice*        pLogicalDevice,
                                std::vector<VkFormat> formats,
                                VkFormatFeatureFlags  features,
                                VkImageTiling         tiling = VK_IMAGE_TILING_OPTIMAL);

    VkFormat getStencilFormat(LogicalDevice* pLogicalDevice);

    bool isDepthFormat(VkFormat format);
    bool isStencilFormat(VkFormat format);
    bool isSRGB(VkFormat format);
    bool isUNORM(VkFormat format);
    bool isExtendedRangeFormat(VkFormat format);
    bool isBGRFormat(VkFormat format);
    bool is10BitPackedFormat(VkFormat format);
    bool isFloatFormat(VkFormat format);
} // namespace vkBasalt

#endif // FORMAT_HPP_INCLUDED
