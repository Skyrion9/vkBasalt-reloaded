#pragma once

#include <memory>
#include <unordered_map>
#include "vulkan_include.hpp"

namespace vkBasalt {
    class Config;
    struct LogicalSwapchain;
    class OverlayManager;

    // Process all hotkeys and reload triggers.Returns true if the current present should be skipped (reload in progress).
    bool processHotkeysAndReloads(
        std::shared_ptr<Config>& pConfig,
        std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>>& swapchainMap,
        OverlayManager& overlayManager
    );
} // namespace vkBasalt
