#include "layer_globals.hpp"
#include "config.hpp"

namespace vkBasalt
{
    std::mutex globalLock; // Synchronization

    std::atomic<uint64_t> g_globalFrameCounter{0};
    std::atomic<uint64_t> g_scopesLastViewedFrame{0};

    // Global config instance. nullptr until ensureConfig() constructs it.
    std::shared_ptr<Config> pConfig = nullptr;

    // Dispatch / object maps
    std::unordered_map<void*, InstanceDispatch> instanceDispatchMap;
    std::unordered_map<void*, VkInstance> instanceMap;
    std::unordered_map<void*, uint32_t> instanceVersionMap;
    std::unordered_map<void*, bool> instanceColorspaceMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>> deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    // Surface tracking
    std::unordered_map<VkSurfaceKHR, SurfacePlatformInfo> g_surfaceMap;

    // Hotkey / UI triggers
    std::atomic<bool> g_triggerSoftReload{false};
    std::atomic<bool> g_triggerHotReload{false};
    std::atomic<bool> g_triggerPreviewReload{false};
    std::atomic<bool> g_triggerRevertReload{false};
    std::atomic<bool> g_effectsEnabled{true};
    std::atomic<bool> g_triggerScreenshot{false};
    std::atomic<bool> g_hdrDebugToolActive{false};
    std::atomic<bool> g_configDirty{true};
    std::atomic<bool> g_frameAnalyzerRequested{false};
    std::atomic<bool> g_scopesUserDisabled{false};

    // Bypass VRAM reclaim timer
    std::chrono::steady_clock::time_point g_effectsDisabledTime;
    bool g_effectsDisabledTimerActive    = false;
    bool g_vramReclaimedForCurrentBypass = false;
} // namespace vkBasalt
