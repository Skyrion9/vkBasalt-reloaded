#pragma once

#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace vkBasalt {

    extern std::atomic<bool> g_triggerScreenshot;

    // Submits a GPU copy of the current frame. Non-blocking the actual file
    // write happens on the next frame via processPendingScreenshot().
    // format: "png", "jpg", "bmp", "tga", "hdr"
    // quality: JPEG quality 1-100 (ignored for other formats)
    void captureScreenshot(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                           uint32_t imageIndex, bool saveBeforeAfter,
                           const std::string& outputPath,
                           const std::string& format = "png",
                           int quality = 95);

    // Returns true if a screenshot GPU copy is in flight.
    bool hasPendingScreenshot();

    // Call only when hasPendingScreenshot() returns true. Polls the GPU fence, reads back pixels when done, and writes the file.
    bool processPendingScreenshot();

} // namespace vkBasalt
