#ifndef LAYER_GLOBALS_HPP_INCLUDED
#define LAYER_GLOBALS_HPP_INCLUDED

#include <vulkan/vulkan_core.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>

#include "logical_device.hpp"
#include "logical_swapchain.hpp"

namespace vkBasalt
{
    class Config; // Forward declaration for pConfig

    // Global config instance (defined in layer_globals.cpp, constructed by ensureConfig in basalt.cpp)
    extern std::shared_ptr<Config> pConfig;

    // Synchronization
    extern std::mutex globalLock;

    // Dispatch / object maps (defined in layer_globals.cpp)
    extern std::unordered_map<void*, InstanceDispatch> instanceDispatchMap;
    extern std::unordered_map<void*, VkInstance> instanceMap;
    extern std::unordered_map<void*, uint32_t> instanceVersionMap;
    extern std::unordered_map<void*, bool> instanceColorspaceMap;
    extern std::unordered_map<void*, std::shared_ptr<LogicalDevice>> deviceMap;
    extern std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    //Surface tracking
    struct SurfacePlatformInfo
    {
        enum class Type { Xlib, Wayland, Unknown } type = Type::Unknown;
        void* display                                   = nullptr;
        void* window                                    = nullptr;
        std::string monitor_name;
    };
    extern std::unordered_map<VkSurfaceKHR, SurfacePlatformInfo> g_surfaceMap;

    // Frame counter + last frame the AutoHDR tab drew with scopes enabled.
    // Used to decide whether scopes are "actively viewed" (see scopesActivelyViewed()).
    extern std::atomic<uint64_t> g_globalFrameCounter;
    extern std::atomic<uint64_t> g_scopesLastViewedFrame;

    // Hotkey / UI triggers
    extern std::atomic<bool> g_triggerSoftReload;
    extern std::atomic<bool> g_triggerHotReload;
    extern std::atomic<bool> g_triggerPreviewReload;
    extern std::atomic<bool> g_triggerRevertReload;
    extern std::atomic<bool> g_effectsEnabled;
    extern std::atomic<bool> g_triggerScreenshot;
    extern std::atomic<bool> g_hdrDebugToolActive;
    extern std::atomic<bool> g_configDirty;
    extern std::atomic<bool> g_frameAnalyzerRequested;

    // Tracks if the user explicitly disabled scopes this session (survives swapchain rebuilds)
    extern std::atomic<bool> g_scopesUserDisabled;

    // Bypass VRAM reclaim timer (defined in layer_globals.cpp)
    extern std::chrono::steady_clock::time_point g_effectsDisabledTime;
    extern bool g_effectsDisabledTimerActive;
    extern bool g_vramReclaimedForCurrentBypass;

} // namespace vkBasalt

#endif // LAYER_GLOBALS_HPP_INCLUDED
