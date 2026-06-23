#ifndef RENDERPASS_HPP_INCLUDED
#define RENDERPASS_HPP_INCLUDED
#pragma once
#include "logical_device.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    // finalLayout defaults to PRESENT_SRC_KHR for final output, but must be SHADER_READ_ONLY_OPTIMAL for intermediate passes (SMAA etc.).
    VkRenderPass createRenderPass(LogicalDevice* pLogicalDevice, 
                                  VkFormat format, 
                                  bool clear = false, 
                                  VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
#endif // RENDERPASS_HPP_INCLUDED
