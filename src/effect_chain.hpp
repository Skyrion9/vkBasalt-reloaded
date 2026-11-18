#pragma once

#include "vulkan_include.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    struct LogicalDevice;
    struct LogicalSwapchain;
    class Config;
    class OverlayManager;

    // Build the full effect chain for a swapchain. Called from vkBasalt_GetSwapchainImagesKHR on initial creation, swapchain != VK_NULL_HANDLE triggers overlay initialization.
    void buildEffectChain(
        LogicalDevice* pDevice,
        LogicalSwapchain* pSwapchain,
        VkSwapchainKHR swapchain,
        Config* pConfig,
        OverlayManager& overlayManager);

    // Destroy and rebuild all effects for a swapchain. Called from hotkey/reload handlers.
    void rebuildEffectChain(
        LogicalDevice* pDevice,
        LogicalSwapchain* pSwapchain,
        Config* pConfig,
        OverlayManager& overlayManager,
        bool waitForIdle = true);

    // Check if HDR output effect (Auto HDR or Calibration) is needed for the given swapchain.
    bool isHdrOutputNeeded(Config* pConfig, LogicalSwapchain* pLogicalSwapchain);

    // Whether the effect writes into an image that must carry VK_IMAGE_USAGE_STORAGE_BIT (compute effects such as CMAA2).
    bool effectRequiresStorageBit(const std::string& effectName);

    // True when the AutoHDR tab has drawn with scopes enabled within the last few frames.
    // Lets passthrough reclaim the fake pool while the checkbox is on but not viewed.
    bool scopesActivelyViewed();

    // Whether the effect can consume and produce the sane image buffer, so it needs no second fake slice.
    // Backed by the factory table, which mirrors Effect::supportsInPlace() on the created object.
    bool effectSupportsInPlace(const std::string& effectName);

    // Whether the last normal effect may render straight into the real swapchain images, skipping the final transfer.
    // Graphics effects need only COLOR_ATTACHMENT_BIT (all swapchain formats support it).
    // Compute effects additionally require the real image to support storage.
    bool canWriteDirectToRealSwapchain(
        LogicalDevice* pLogicalDevice, VkFormat realSwapchainFormat, const std::string& effectName);

    // How many fake image slices the chain requires. effectsBypassed treats the configured chain as inert (the active chain is just a slice 0 transfer).
    uint32_t getRequiredFakeSlices(
        LogicalDevice* pLogicalDevice,
        VkFormat realSwapchainFormat,
        const std::vector<std::string>& effectStrings,
        bool hdrOutputNeeded,
        bool effectsBypassed);

    // Free command buffers, clear effects, and build the default no-effect fallback chain.
    void rebuildFallbackChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, Config* pConfig);

    // Remove empty strings that the config parser produces for an empty "effects =" option (parses as [""]).
    // Unfiltered effect lists missize the fake image pool as an empty name may appear like a valid, real effect.
    void pruneEmptyEffectNames(std::vector<std::string>& effectStrings);
} // namespace vkBasalt
