#ifndef LOGICAL_SWAPCHAIN_HPP_INCLUDED
#define LOGICAL_SWAPCHAIN_HPP_INCLUDED

#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

#include "effect.hpp"
#include "vulkan_include.hpp"
#include "logical_device.hpp"
#include "compute_pass.hpp"
#include "effect_timing.hpp"

namespace vkBasalt
{

    // for each swapchain, we have the Images and the other stuff we need to execute the compute shader
    struct LogicalSwapchain
    {
        LogicalDevice* pLogicalDevice{};
        VkSwapchainCreateInfoKHR swapchainCreateInfo{};
        VkExtent2D imageExtent{};

        // Legacy compatibility, equals destFormat/destColorSpace
        VkFormat format;
        VkColorSpaceKHR colorSpace;

        // Dual format tracking for Auto HDR (SDR fake images -> HDR real swapchain)
        VkFormat sourceFormat;
        VkColorSpaceKHR sourceColorSpace;
        VkFormat destFormat;
        VkColorSpaceKHR destColorSpace;
        bool autoHdrActive                 = false;
        bool swapchainMutableFormat        = false;
        VkImageUsageFlags actualImageUsage = 0; // The actual usage flags the swapchain images were created with

        // Keep the format list and pNext chain copies alive for the lifetime of the swapchain.
        // The validation layer caches the VkImageFormatListCreateInfo pointer for the VkImage so the memory must not be freed until vkDestroySwapchainKHR.
        std::vector<VkFormat> mergedViewFormats;
        std::vector<std::unique_ptr<uint8_t[]>> pNextChainCopies;

        // Passthrough mode states
        std::chrono::steady_clock::time_point passthroughTimerStart;
        bool passthroughEligible    = false;
        bool passthroughTimerActive = false;
        bool passthroughActive      = false;

        uint32_t imageCount{};
        std::vector<VkImage> images;
        std::vector<VkImage> fakeImages;
        std::vector<VkCommandBuffer> commandBuffersEffect;
        std::vector<VkCommandBuffer> commandBuffersNoEffect;
        std::vector<VkSemaphore> semaphores;
        std::vector<std::shared_ptr<Effect>> effects;
        std::shared_ptr<Effect> defaultTransfer;
        std::shared_ptr<Effect> defaultHdrEffect; // HDR conversion for empty/disabled chain when Auto HDR is active
        std::shared_ptr<Effect> defaultHdrDebugEffect; // HDR debug pattern for bypassed chain
        VkDeviceMemory fakeImageMemory{};

        // flag to force the game to recreate the swapchain if the effect chain grows dynamically.
        // prevents device loss by letting the game engine cleanly release its cached VkImage handles.
        bool forceSwapchainRebuild = false;

        // Thread safe runtime state management for ImGui
        mutable std::mutex effectMutex;
        std::atomic<bool> needsRecreation{false};

        // Fence based deferred rebuild (avoids QueueWaitIdle deadlock in QueuePresentKHR)
        VkFence rebuildFence = VK_NULL_HANDLE;
        bool pendingRebuild  = false;

        std::vector<std::shared_ptr<ComputePass>> computePasses;
        // Graveyard for deferred destruction. Keeps old FrameAnalyzers alive until the swapchain is destroyed.
        // This prevents the GPU from reading freed memory if a soft reload occurs, while  the previous frame's overlay command buffer is still in flight.
        std::vector<std::shared_ptr<ComputePass>> computePassGraveyard;
        uint32_t computeSrcSlice     = 0;     // Which slice compute passes read from
        uint32_t finalFakeSlice      = 0;     // Tracks the final active fake slice after all effects have run
        bool lastEffectWroteToReal   = false; // True if the last effect wrote directly to real swapchain images
        Effect* nitCalibrationEffect = nullptr;
        std::string monitorName; // Platform detected physical connector (e.g., "DP-1")

        // X11 input target for the surface this swapchain renders into. CEF games create several
        // surfaces on different windows, so the last created global window is not necessarily ours.
        void* surfaceDisplay = nullptr;
        void* surfaceWindow  = nullptr;

        // Two chains because the bypass fallback records a different effect list than the main
        // chain, so their query layouts differ.
        ChainTiming timingMain;
        ChainTiming timingBypass;

        void destroy();
    };
} // namespace vkBasalt

#endif // LOGICAL_SWAPCHAIN_HPP_INCLUDED
