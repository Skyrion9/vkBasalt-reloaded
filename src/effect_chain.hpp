#pragma once

#include "vulkan_include.hpp"
#include <cstdint>
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

    // Check if HDR output effect (Auto HDR or Calibration) is needed for the given swapchain.
    bool isHdrOutputNeeded(Config* pConfig, LogicalSwapchain* pLogicalSwapchain);

    // Calculate the total number of effects in the chain, accounting for HDR output and bypass state.
    uint32_t calculateTotalEffectCount(Config* pConfig, LogicalSwapchain* pLogicalSwapchain);

    // Free command buffers, clear effects, and build the default no-effect fallback chain.
    void rebuildFallbackChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, Config* pConfig);
} // namespace vkBasalt
