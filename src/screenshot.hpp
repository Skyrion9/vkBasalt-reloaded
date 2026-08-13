#pragma once

#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include <atomic>
#include <string>

namespace vkBasalt {

    extern std::atomic<bool> g_triggerScreenshot;

    // Captures the current frame. If saveBeforeAfter is true, saves both
    // the game output (fakeImages[imageIndex]) and the post-processed output
    // (images[imageIndex]). Otherwise saves only the post-processed output.
    void captureScreenshot(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                           uint32_t imageIndex, bool saveBeforeAfter,
                           const std::string& outputPath);

} // namespace vkBasalt
