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

    enum class ColorSpaceMode : int32_t {
        SDR_SRGB              = 0, // sRGB transfer function (ALU decode/encode)
        HDR10_PQ              = 1, // ST 2084 (PQ) - Includes HDR10, HDR12, Dolby Vision Profile 8
        HDR_HLG               = 2, // Hybrid Log/Gamma (Broadcast HDR)
        HDR_SCRGB             = 3, // Linear Float (1.0 = 80 nits SDR white)
        HDR_BT2020_LINEAR     = 4, // Linear Float (BT.2020 Primaries, 1.0 = 80 nits SDR white)
        HDR_DISPLAY_P3_LINEAR = 5, // Linear Float (Display P3 Primaries, 1.0 = 80 nits SDR white)
        DISPLAY_P3_NONLINEAR  = 6  // sRGB/Gamma Float (Display P3 Primaries)
    };

    ColorSpaceMode getColorSpaceMode(VkFormat format, VkColorSpaceKHR colorSpace);
} // namespace vkBasalt

#endif // FORMAT_HPP_INCLUDED
