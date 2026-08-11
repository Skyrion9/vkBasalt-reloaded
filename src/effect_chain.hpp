#pragma once

#include "vulkan_include.hpp"
#include <vector>

namespace vkBasalt {
    struct LogicalDevice;
    struct LogicalSwapchain;
    class Config;
    class OverlayManager;

    // Build the full effect chain for a swapchain. Called from vkBasalt_GetSwapchainImagesKHR on initial creation, swapchain != VK_NULL_HANDLE triggers overlay initialization.
    void buildEffectChain(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                          VkSwapchainKHR swapchain, Config* pConfig,
                          OverlayManager& overlayManager);

    // Destroy and rebuild all effects for a swapchain. Called from hotkey/reload handlers.
    void rebuildEffectChain(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                            Config* pConfig, OverlayManager& overlayManager,
                            bool waitForIdle = true);
} // namespace vkBasalt
