#include "vkdispatch.hpp"
#include <X11/X.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <sys/types.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_wayland.h>
#define VK_USE_PLATFORM_WAYLAND_KHR
#include "vulkan_include.hpp"

#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib.h>
#undef None
#undef Bool
#undef Status
#undef Always
#undef Success
#undef True
#undef False

#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <string>
#include <memory>
#include <cstring>
#include <algorithm>
#include <unistd.h>

#include "util.hpp"

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include "keyboard_input_wayland.hpp"
#endif

#include "keyboard_input.hpp"
#include "keyboard_input_x11.hpp"
#include "hotkey_manager.hpp"
#include "imgui_overlay.hpp"
#include "overlay_manager.hpp"
#include "effect_chain.hpp"

#include "logical_device.hpp"
#include "logical_swapchain.hpp"

#include "image_view.hpp"
#include "pipeline_cache.hpp"
#include "command_buffer.hpp"
#include "config.hpp"
#include "fake_swapchain.hpp"
#include "screenshot.hpp"
#include "format.hpp"
#include "logger.hpp"
#include "fake_image_tracker.hpp"
#include "layer_globals.hpp"

#define VKBASALT_NAME "VK_LAYER_VKBASALT_post_processing"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    Logger Logger::s_instance;
    pid_t g_layer_init_pid = 0;

    static OverlayManager g_overlayManager;

#ifdef VK_USE_PLATFORM_XLIB_KHR
    static std::string getX11MonitorName(Display* dpy, Window win)
    {
        if (!dpy || !win) return "";
        int x = 0, y = 0;
        Window child = 0;
        XWindowAttributes win_attrs;
        if (!XGetWindowAttributes(dpy, win, &win_attrs)) return "";

        XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);
        int win_center_x = x + (win_attrs.width / 2);
        int win_center_y = y + (win_attrs.height / 2);

        int nmonitors            = 0;
        XRRMonitorInfo* monitors = XRRGetMonitors(dpy, DefaultRootWindow(dpy), 1, &nmonitors);
        std::string monitor_name = "";

        if (monitors) {
            for (int i = 0; i < nmonitors; ++i) {
                if (win_center_x >= monitors[i].x && win_center_x < (monitors[i].x + monitors[i].width)
                    && win_center_y >= monitors[i].y && win_center_y < (monitors[i].y + monitors[i].height)) {
                    char* atom_name = XGetAtomName(dpy, monitors[i].name);
                    if (atom_name) {
                        monitor_name = atom_name;
                        XFree(atom_name);
                    }
                    break;
                }
            }
            XRRFreeMonitors(monitors);
        }
        return monitor_name;
    }
#endif

#ifdef _GCC_
    using scoped_lock __attribute__((unused)) = std::lock_guard<std::mutex>;
#else
    using scoped_lock = std::scoped_lock<std::mutex>;
#endif

    static void ensureConfig()
    {
        if (pConfig == nullptr) {
            std::scoped_lock l(globalLock);
            if (pConfig == nullptr) {
                pConfig = std::make_shared<Config>();
            }
        }
    }

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(
        const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
    {
        if (g_layer_init_pid == 0) {
            g_layer_init_pid = getpid();
        }
        auto* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;
        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
                   || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }
        Logger::trace("vkCreateInstance ENTERED");

        if (pCreateInfo && pCreateInfo->pApplicationInfo) {
            Logger::info(
                "vkCreateInstance: Game requested API version "
                + std::to_string(VK_API_VERSION_MAJOR(pCreateInfo->pApplicationInfo->apiVersion)) + "."
                + std::to_string(VK_API_VERSION_MINOR(pCreateInfo->pApplicationInfo->apiVersion)));
        } else {
            Logger::info("vkCreateInstance: Game requested API version (null pApplicationInfo, defaulting to 1.0)");
        }

        if (layerCreateInfo == nullptr) {
            Logger::err("vkCreateInstance FAILED: No loader instance create info");
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // 1. Get the function pointer FIRST
        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;

        // 2. Move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
        auto createFunc               = reinterpret_cast<PFN_vkCreateInstance>(gpa(VK_NULL_HANDLE, "vkCreateInstance"));
        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;

        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount) {
            enabledExtensionNames = std::vector<const char*>(
                modifiedCreateInfo.ppEnabledExtensionNames,
                modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        // Always attempt to inject VK_EXT_swapchain_colorspace for AutoHDR.
        addUniqueCString(enabledExtensionNames, "VK_EXT_swapchain_colorspace");
        // VK_EXT_swapchain_colorspace strictly requires VK_KHR_surface. If the game/loader didn't explicitly add it,
        // The validation layer will flag VUID-vkCreateInstance-ppEnabledExtensionNames-01388.
        addUniqueCString(enabledExtensionNames, "VK_KHR_surface");

        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        // Advertise Vulkan 1.3 downstream.DXVK's HDR path and vkBasalt's HDR swapchain mutation rely on this.
        VkApplicationInfo appInfo{};
        if (modifiedCreateInfo.pApplicationInfo) {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_3) {
                appInfo.apiVersion = VK_API_VERSION_1_3;
            }
        } else {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_3;
        }
        modifiedCreateInfo.pApplicationInfo = &appInfo;

        Logger::info("vkCreateInstance: Calling next layer's vkCreateInstance (with colorspace ext)...");
        VkResult ret = createFunc(&modifiedCreateInfo, pAllocator, pInstance);

        bool supportsSwapchainColorspace = true;
        if (ret == VK_ERROR_EXTENSION_NOT_PRESENT) {
            Logger::warn("VK_EXT_swapchain_colorspace not supported by driver, retrying without it...");
            // Remove it and retry
            enabledExtensionNames.erase(
                std::remove_if(
                    enabledExtensionNames.begin(), enabledExtensionNames.end(),
                    [](const char* ext) { return std::strcmp(ext, "VK_EXT_swapchain_colorspace") == 0; }),
                enabledExtensionNames.end());
            modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
            modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();
            ret                                        = createFunc(&modifiedCreateInfo, pAllocator, pInstance);
            supportsSwapchainColorspace                = false;
        }

        if (ret != VK_SUCCESS) {
            Logger::err("vkCreateInstance FAILED with VkResult: " + std::to_string(static_cast<int>(ret)));
            return ret;
        }
        Logger::info("vkCreateInstance SUCCEEDED");

        // fetch our own dispatch table for the functions we need, into the next layer
        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        // store the table by key
        {
            std::scoped_lock l(globalLock);
            instanceDispatchMap[GetKey(*pInstance)]   = dispatchTable;
            instanceMap[GetKey(*pInstance)]           = *pInstance;
            instanceVersionMap[GetKey(*pInstance)]    = appInfo.apiVersion;
            instanceColorspaceMap[GetKey(*pInstance)] = supportsSwapchainColorspace;
        }

        Logger::info(
            "vkCreateInstance RETURNING success to app, instance="
            + std::to_string(reinterpret_cast<uintptr_t>(*pInstance))
            + ", colorspaceExt=" + std::to_string(supportsSwapchainColorspace)
            + ", usedApiVersion=" + std::to_string(VK_API_VERSION_MAJOR(appInfo.apiVersion)) + "."
            + std::to_string(VK_API_VERSION_MINOR(appInfo.apiVersion)) + "."
            + std::to_string(VK_API_VERSION_PATCH(appInfo.apiVersion)));

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        Logger::trace("vkDestroyInstance ENTERED, instance=" + std::to_string(reinterpret_cast<uintptr_t>(instance)));

        if (!instance) return;
        std::scoped_lock l(globalLock);
        Logger::trace("vkDestroyInstance");
        // Don't shutdownWaylandInput() here. Games with launchers (Naraka/NEAC) destroy the launcher instance and create a new one for
        // the main game. Destroying Wayland input here kills hotkeys for the main game instance.
        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        dispatchTable.DestroyInstance(instance, pAllocator);
        instanceDispatchMap.erase(GetKey(instance));
        instanceMap.erase(GetKey(instance));
        instanceVersionMap.erase(GetKey(instance));
        instanceColorspaceMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice)
    {
        Logger::trace(
            "vkCreateDevice ENTERED, physicalDevice=" + std::to_string(reinterpret_cast<uintptr_t>(physicalDevice)));
        std::scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        auto* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                   || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr) {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr gdpa   = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        auto createFunc = reinterpret_cast<PFN_vkCreateDevice>(gipa(VK_NULL_HANDLE, "vkCreateDevice"));

        // check and activate extentions
        uint32_t extensionCount = 0;

        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat = false;
        bool supportsHdrMetadata   = false;
        for (VkExtensionProperties properties : extensionProperties) {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format")) {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
            } else if (properties.extensionName == std::string("VK_EXT_hdr_metadata")) {
                Logger::debug("device supports VK_EXT_hdr_metadata");
                supportsHdrMetadata = true;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount) {
            enabledExtensionNames = std::vector<const char*>(
                modifiedCreateInfo.ppEnabledExtensionNames,
                modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        if (supportsMutableFormat) {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
        }
        if (supportsHdrMetadata) {
            Logger::debug("activating hdr_metadata");
            addUniqueCString(enabledExtensionNames, "VK_EXT_hdr_metadata");
        }
        if (deviceProps.apiVersion < VK_API_VERSION_1_2
            || instanceVersionMap[GetKey(physicalDevice)] < VK_API_VERSION_1_2) {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        // Active needed Features
        VkPhysicalDeviceFeatures deviceFeatures = {};
        if (modifiedCreateInfo.pEnabledFeatures) {
            deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
        }
        deviceFeatures.shaderImageGatherExtended = VK_TRUE;
        // Required for CMAA2 to write to BGRA8 swapchain images without explicit rgba8 format qualifiers,
        // Fixes 0x13365b2 validation warning without using illegal storage image swizzles.
        deviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        modifiedCreateInfo.pEnabledFeatures                 = &deviceFeatures;

        // Force enable vulkanMemoryModelDeviceScope to prevent VUID-RuntimeSpirv-vulkanMemoryModel-06265.
        // Some of our shaders use atomic operations with Device scope.
        bool foundMemModel = false;

        // Safely cast away constness from pNext to walk and potentially modify the chain
        void** ppNext               = const_cast<void**>(&modifiedCreateInfo.pNext);
        VkBaseOutStructure* current = reinterpret_cast<VkBaseOutStructure*>(*ppNext);

        while (current) {
            if (current->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES) {
                auto* existing = reinterpret_cast<VkPhysicalDeviceVulkanMemoryModelFeatures*>(current);
                existing->vulkanMemoryModelDeviceScope = VK_TRUE;
                foundMemModel                          = true;
                break;
            }
            if (current->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
                auto* existing                         = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(current);
                existing->vulkanMemoryModelDeviceScope = VK_TRUE;
                foundMemModel                          = true;
                break;
            }
            ppNext  = reinterpret_cast<void**>(&current->pNext);
            current = current->pNext;
        }

        if (!foundMemModel) {
            // Safe to use static here because globalLock is held at the top of vkBasalt_CreateDevice
            static VkPhysicalDeviceVulkanMemoryModelFeatures s_memModelFeatures{};
            s_memModelFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
            s_memModelFeatures.pNext             = *ppNext;
            s_memModelFeatures.vulkanMemoryModel = VK_TRUE;
            s_memModelFeatures.vulkanMemoryModelDeviceScope = VK_TRUE;
            *ppNext                                         = &s_memModelFeatures;
        }

        VkResult ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

        if (ret != VK_SUCCESS) return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice = std::make_shared<LogicalDevice>();
        pLogicalDevice->vki                           = instanceDispatchMap[GetKey(physicalDevice)];
        pLogicalDevice->device                        = *pDevice;
        pLogicalDevice->physicalDevice                = physicalDevice;
        pLogicalDevice->instance                      = instanceMap[GetKey(physicalDevice)];
        pLogicalDevice->queue                         = VK_NULL_HANDLE;
        pLogicalDevice->queueFamilyIndex              = 0;
        pLogicalDevice->commandPool                   = VK_NULL_HANDLE;
        pLogicalDevice->supportsMutableFormat         = supportsMutableFormat;
        pLogicalDevice->supportsSwapchainColorspace   = instanceColorspaceMap[GetKey(pLogicalDevice->instance)];
        pLogicalDevice->supportsHdrMetadata           = supportsHdrMetadata;
        pLogicalDevice->physicalDeviceProperties      = deviceProps;

        fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);

        uint32_t count = 0;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(
            pLogicalDevice->physicalDevice, &count, queueProperties.data());
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                pLogicalDevice->vkd.GetDeviceQueue(
                    pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                pLogicalDevice->vkd.CreateCommandPool(
                    pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue) Logger::err("Did not find a graphics queue!");
        // Cache memory properties before any fork so ImGui can use them without calling vkGetPhysicalDeviceMemoryProperties through the loader
        pLogicalDevice->vki.GetPhysicalDeviceMemoryProperties(
            pLogicalDevice->physicalDevice, &pLogicalDevice->memoryProperties);

        // Initialize pipeline cache (loaded from disk, saved on device destroy)
        pLogicalDevice->pipelineCachePath   = getPipelineCachePath(pLogicalDevice->physicalDevice, pLogicalDevice->vki);
        VkPipelineCacheCreateInfo cacheInfo = {};
        cacheInfo.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        std::vector<uint8_t> cacheData      = loadPipelineCacheData(pLogicalDevice->pipelineCachePath);
        if (!cacheData.empty()) {
            cacheInfo.initialDataSize = cacheData.size();
            cacheInfo.pInitialData    = cacheData.data();
        }
        pLogicalDevice->vkd.CreatePipelineCache(
            pLogicalDevice->device, &cacheInfo, nullptr, &pLogicalDevice->pipelineCache);
        deviceMap[GetKey(*pDevice)] = pLogicalDevice;
        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device) return;

        std::scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        // Save pipeline cache to disk before destroying
        savePipelineCacheData(
            device, pLogicalDevice->vkd, pLogicalDevice->pipelineCache, pLogicalDevice->pipelineCachePath);

        if (pLogicalDevice->pipelineCache != VK_NULL_HANDLE) {
            pLogicalDevice->vkd.DestroyPipelineCache(device, pLogicalDevice->pipelineCache, nullptr);
            pLogicalDevice->pipelineCache = VK_NULL_HANDLE;
        }

        if (pLogicalDevice->commandPool != VK_NULL_HANDLE) {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, pAllocator);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        deviceMap.erase(GetKey(device));
    }

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateWaylandSurfaceKHR(
        VkInstance instance,
        const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSurfaceKHR* pSurface)
    {
        std::scoped_lock l(globalLock);
        Logger::trace("vkCreateWaylandSurfaceKHR");

        // Grab display pointer
        if (pCreateInfo && pCreateInfo->display) {
            // Skip Wayland input init under Wine/Proton to avoid conflicting with winewayland.so's event loop
            // which causes deadlocks during vkCreateWaylandSurfaceKHR or the first QueuePresentKHR.
            bool isWine =
                (getenv("WINELOADER") != nullptr || getenv("WINEDLLPATH") != nullptr || getenv("WINEPREFIX") != nullptr
                 || getenv("STEAM_COMPAT_DATA_PATH") != nullptr);
            if (!isWine) {
                setInputBackend(true); // Confirm Wayland backend
                initWaylandInput(
                    reinterpret_cast<void*>(pCreateInfo->display), reinterpret_cast<void*>(pCreateInfo->surface));
            } else {
                Logger::info("Using Wine-safe pointer-only Wayland input with X11 keyboard fallback");
                setInputBackend(false); // Keep keyboard/hotkeys on X11

                const char* winePointerEnv = getenv("VKBASALT_WINE_WAYLAND_POINTER");
                if (!winePointerEnv || std::strcmp(winePointerEnv, "0") != 0) {
                    initWaylandPointerOnly(
                        reinterpret_cast<void*>(pCreateInfo->display), reinterpret_cast<void*>(pCreateInfo->surface));
                }
            }
        }

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        auto fpCreateWaylandSurfaceKHR = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            dispatchTable.GetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));

        if (fpCreateWaylandSurfaceKHR) {
            return fpCreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
        }

        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXlibSurfaceKHR(
        VkInstance instance,
        const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSurfaceKHR* pSurface)
    {
        std::scoped_lock l(globalLock);
        Logger::trace("vkCreateXlibSurfaceKHR");
        // Grab display pointer and window handle for X11 input
        if (pCreateInfo && pCreateInfo->dpy) {
            setInputBackend(false); // Confirm X11/XWayland backend
            initX11Input(
                reinterpret_cast<void*>(pCreateInfo->dpy), (void*) static_cast<uintptr_t>(pCreateInfo->window));
        }
        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        auto fpCreateXlibSurfaceKHR    = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
            dispatchTable.GetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
        if (fpCreateXlibSurfaceKHR) {
            VkResult res = fpCreateXlibSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
            if (res == VK_SUCCESS) {
                SurfacePlatformInfo info;
                info.type         = SurfacePlatformInfo::Type::Xlib;
                info.display      = reinterpret_cast<void*>(pCreateInfo->dpy);
                info.window       = (void*) pCreateInfo->window;
                info.monitor_name = getX11MonitorName(pCreateInfo->dpy, pCreateInfo->window);
                if (!info.monitor_name.empty()) {
                    Logger::info("X11 Surface mapped to monitor: " + info.monitor_name);
                }
                g_surfaceMap[*pSurface] = info;
            }
            return res;
        }
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
#endif

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        std::scoped_lock l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain = std::make_shared<LogicalSwapchain>();
        pLogicalSwapchain->pLogicalDevice                   = pLogicalDevice;

        std::vector<VkFormat> mergedFormats;
        std::vector<std::unique_ptr<uint8_t[]>> pNextCopies;

        // 1. Extract game's format list if any
        const VkImageFormatListCreateInfo* gameFormatList = nullptr;
        const void* currentPNext                          = pCreateInfo->pNext;
        while (currentPNext) {
            const VkBaseInStructure* base = reinterpret_cast<const VkBaseInStructure*>(currentPNext);
            if (base->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
                gameFormatList = reinterpret_cast<const VkImageFormatListCreateInfo*>(base);
            }
            currentPNext = base->pNext;
        }

        // 2. Build merged format list
        if (gameFormatList && gameFormatList->viewFormatCount > 0 && gameFormatList->pViewFormats) {
            for (uint32_t i = 0; i < gameFormatList->viewFormatCount; i++) {
                mergedFormats.push_back(gameFormatList->pViewFormats[i]);
            }
        } else {
            mergedFormats.push_back(unormFormat);
            if (srgbFormat != unormFormat) {
                mergedFormats.push_back(srgbFormat);
            }
        }

        auto addFormat = [&](VkFormat fmt) {
            if (fmt != VK_FORMAT_UNDEFINED
                && std::find(mergedFormats.begin(), mergedFormats.end(), fmt) == mergedFormats.end()) {
                mergedFormats.push_back(fmt);
            }
        };

        // Fake images are created as UNORM so compute effects can write them.
        // That format must be present in the view format list, even when the game's own list omitted the UNORM variant.
        addFormat(unormFormat);

        // Injecting VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR flag breaks direct scanout on Linux compositors.
        // However, if Auto HDR mutates the format, we MUST set it so the game can still create SDR views on the HDR images.
        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // Required for screenshot readback

        // Storage usage is needed when compute effects write directly to real swapchain images.
        if (pLogicalDevice->supportsMutableFormat
            && supportsStorageImage(pLogicalDevice, modifiedCreateInfo.imageFormat)) {
            modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        } else {
            // If the format doesn't support storage, strip STORAGE_BIT even if the game requested it.
            // This prevents validation errors when the game later creates image views that inherit the image usage.
            modifiedCreateInfo.imageUsage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
        }

        // 3. Rebuild pNext chain, skipping the game's old format list
        VkBaseOutStructure* newPNextHead  = nullptr;
        VkBaseOutStructure** newPNextTail = &newPNextHead;

        currentPNext = pCreateInfo->pNext;
        while (currentPNext) {
            const VkBaseInStructure* base = reinterpret_cast<const VkBaseInStructure*>(currentPNext);
            if (base->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
                currentPNext = base->pNext;
                continue; // Skip the game's format list
            }

            size_t structSize = 0;
            switch (base->sType) {
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT:
                    structSize = sizeof(VkSwapchainPresentModesCreateInfoEXT);
                    break;
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT:
                    structSize = sizeof(VkSwapchainPresentScalingCreateInfoEXT);
                    break;
                case VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR:
                    structSize = sizeof(VkDeviceGroupSwapchainCreateInfoKHR);
                    break;
                case VK_STRUCTURE_TYPE_SWAPCHAIN_COUNTER_CREATE_INFO_EXT:
                    structSize = sizeof(VkSwapchainCounterCreateInfoEXT);
                    break;
                default: structSize = 0; break; // Unknown: pass through original pointer
            }

            if (structSize > 0) {
                // Known struct: deep copy with exact size (no truncation risk)
                auto copy = std::make_unique<uint8_t[]>(structSize);
                std::memcpy(copy.get(), base, structSize);

                VkBaseOutStructure* newBase = reinterpret_cast<VkBaseOutStructure*>(copy.get());
                newBase->pNext              = nullptr;
                *newPNextTail               = newBase;
                newPNextTail                = &newBase->pNext;

                pNextCopies.push_back(std::move(copy));
            } else {
                // Unknown struct: pass through the original pointer directly. Safe as vkCreateSwapchainKHR is called synchronously on this thread.
                // The game's original structs are still alive. Temporarily overwrite pNext to relink the chain (skipping the format list).
                VkBaseOutStructure* passthrough =
                    const_cast<VkBaseOutStructure*>(reinterpret_cast<const VkBaseOutStructure*>(base));
                *newPNextTail = passthrough;
                newPNextTail  = &passthrough->pNext;
            }
            currentPNext = base->pNext;
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        Logger::debug("colorSpace " + std::to_string(modifiedCreateInfo.imageColorSpace));

        pLogicalSwapchain->swapchainCreateInfo = *pCreateInfo;
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->sourceFormat        = pCreateInfo->imageFormat;
        pLogicalSwapchain->sourceColorSpace    = pCreateInfo->imageColorSpace;
        pLogicalSwapchain->destFormat          = pCreateInfo->imageFormat;
        pLogicalSwapchain->destColorSpace      = pCreateInfo->imageColorSpace;

        // Platform monitor detection: Map surface to physical connector name
        auto surfIt = g_surfaceMap.find(pCreateInfo->surface);
        if (surfIt != g_surfaceMap.end()) {
            pLogicalSwapchain->monitorName = surfIt->second.monitor_name;
#ifdef VK_USE_PLATFORM_XLIB_KHR
            // Re-evaluate X11 position in case window moved since surface creation
            if (surfIt->second.type == SurfacePlatformInfo::Type::Xlib) {
                std::string currentMonitor =
                    getX11MonitorName(static_cast<Display*>(surfIt->second.display), (Window) surfIt->second.window);
                if (!currentMonitor.empty()) {
                    pLogicalSwapchain->monitorName = currentMonitor;
                }
            }
#endif
        }

        // Auto HDR: Mutate real swapchain to HDR10 if display supports it and config is enabled
        auto autoHdrOpt       = pConfig->getOption<std::string>("autoHdr", "on");
        bool autoHdrEnabled   = (autoHdrOpt == "on" || autoHdrOpt == "true" || autoHdrOpt == "1");
        ColorSpaceMode srcCsm = getColorSpaceMode(pCreateInfo->imageFormat, pCreateInfo->imageColorSpace);

        // For PGO training force HDR mutation even if AutoHDR is disabled in config.
        bool forceHdrPgo = (std::getenv("VKBASALT_FORCE_HDR_PGO") != nullptr);
        if (forceHdrPgo) {
            Logger::info("PGO Training: Forcing HDR swapchain mutation via VKBASALT_FORCE_HDR_PGO");
        }

        // Auto HDR requires mutable format to bridge SDR fake images and HDR real swapchain images.
        // Skip HDR mutation on X11/Xwayland surfaces as X11 compositors don't support HDR.
        bool isX11Surface = (surfIt != g_surfaceMap.end() && surfIt->second.type == SurfacePlatformInfo::Type::Xlib);

        Logger::trace(
            "CreateSwapchainKHR: autoHdr config='" + autoHdrOpt + "' forceHdrPgo=" + std::to_string(forceHdrPgo)
            + " passthroughActive=" + std::to_string(pLogicalSwapchain->passthroughActive)
            + " isX11Surface=" + std::to_string(isX11Surface));

        // Auto HDR: mutate swapchain to HDR format/colorspace if SDR and autoHdr is enabled
        Logger::debug(
            "AutoHDR check: srcCsm=" + std::to_string(static_cast<int>(srcCsm)) + " autoHdrEnabled="
            + std::to_string(autoHdrEnabled) + " forceHdrPgo=" + std::to_string(forceHdrPgo) + " supportsMutableFormat="
            + std::to_string(pLogicalDevice->supportsMutableFormat) + " isX11Surface=" + std::to_string(isX11Surface));

        // Strictly require the instance extension to be enabled. If it wasn't supported by the driver,
        // We can't mutate the swapchain to HDR10 without triggering validation errors and game crashes.
        if (((srcCsm == ColorSpaceMode::SDR_SRGB && autoHdrEnabled) || forceHdrPgo)
            && pLogicalDevice->supportsMutableFormat && pLogicalDevice->supportsSwapchainColorspace && !isX11Surface) {
            uint32_t formatCount = 0;
            pLogicalDevice->vki.GetPhysicalDeviceSurfaceFormatsKHR(
                pLogicalDevice->physicalDevice, pCreateInfo->surface, &formatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
            pLogicalDevice->vki.GetPhysicalDeviceSurfaceFormatsKHR(
                pLogicalDevice->physicalDevice, pCreateInfo->surface, &formatCount, surfaceFormats.data());

            struct HdrTarget
            {
                VkFormat format;
                VkColorSpaceKHR colorSpace;
                int priority; // lower is better
            };
            HdrTarget bestTarget = {
                .format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .priority = 999};

            for (const auto& f : surfaceFormats) {
                int prio = 999;
                // 1. HDR10 PQ (10 bit) - Standard for HDR TVs/Monitors
                // Prefer A2B10G10R10 (Rgb10A2) because our compute shaders (HdrDebugEffect) are compiled expecting this format for storage images.
                // Using A2R10G10B10 causes Undefined-Value-StorageImage-FormatMismatch-ImageView validation warnings.
                if (f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT && f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
                    prio = 1;
                }
                // 2. HDR 10 PQ (10 bit) - ARGB
                else if (
                    f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT && f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
                    prio = 2;
                }
                // 3. scRGB (FP16 linear) - Standard Windows HDR path
                else if (
                    f.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
                    && f.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                    prio = 3;
                }
                // 4. HLG (10 bit) - Broadcast standard
                else if (
                    f.colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT
                    && (f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
                        || f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)) {
                    prio = 4;
                }
                // 5. BT.2020 Linear (FP16 linear) - Wide Gamut Linear
                else if (
                    f.colorSpace == VK_COLOR_SPACE_BT2020_LINEAR_EXT && f.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                    prio = 5;
                }
                // 6. Display P3 Linear (FP16 linear) - Wide Gamut Linear
                else if (
                    f.colorSpace == VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT && f.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                    prio = 6;
                }
                if (prio < bestTarget.priority) {
                    bestTarget = {.format = f.format, .colorSpace = f.colorSpace, .priority = prio};
                }
            }

            if (bestTarget.format != VK_FORMAT_UNDEFINED) {
                modifiedCreateInfo.imageFormat     = bestTarget.format;
                modifiedCreateInfo.imageColorSpace = bestTarget.colorSpace;
                // Must flag as mutable and add the HDR format to the view list, otherwise the game will crash when creating SDR ImageViews on the HDR swapchain images.
                modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

                addFormat(bestTarget.format);
                addFormat(getAlternate10BitFormat(bestTarget.format));

                pLogicalSwapchain->destFormat             = modifiedCreateInfo.imageFormat;
                pLogicalSwapchain->destColorSpace         = modifiedCreateInfo.imageColorSpace;
                pLogicalSwapchain->autoHdrActive          = true;
                pLogicalSwapchain->swapchainMutableFormat = true;

                // If mutated to HDR, can't use passthrough mode. Passthrough returns real images to the game.
                // Force fake image allocation and build the NitCalibrationEffect.
                pLogicalSwapchain->passthroughActive      = false;
                pLogicalSwapchain->passthroughTimerActive = false;
                pLogicalSwapchain->passthroughEligible    = false;

                Logger::info(
                    "Auto HDR: Mutating swapchain to format " + std::to_string(bestTarget.format) + " / colorspace "
                    + std::to_string(bestTarget.colorSpace) + " (forced passthroughActive=false)");
            }
        }

        pLogicalSwapchain->format       = pLogicalSwapchain->destFormat;
        pLogicalSwapchain->colorSpace   = pLogicalSwapchain->destColorSpace;
        pLogicalDevice->swapchainFormat = pLogicalSwapchain->destFormat;
        pLogicalSwapchain->imageCount   = 0;

        // Non-HDR direct write mutable format setup
        if (!pLogicalSwapchain->autoHdrActive && pLogicalDevice->supportsMutableFormat) {
            VkFormat directWriteViewFormat = convertToUNORM(modifiedCreateInfo.imageFormat);
            if (directWriteViewFormat != VK_FORMAT_UNDEFINED
                && directWriteViewFormat != modifiedCreateInfo.imageFormat) {
                addFormat(directWriteViewFormat);
                modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
                pLogicalSwapchain->swapchainMutableFormat = true;

                Logger::debug(
                    "CreateSwapchainKHR: enabling non-HDR mutable swapchain for direct final effect writes, "
                    "base format="
                    + std::to_string(modifiedCreateInfo.imageFormat)
                    + " view format=" + std::to_string(directWriteViewFormat));
            }
        }

        // If the merged format list has more than 1 format, we've to set the mutable format flag.
        // This satisfies VUID-VkSwapchainCreateInfoKHR-flags-04100, which requires the flag if viewFormatCount > 1.
        // The game may have forgotten it, or we added HDR/direct write formats ourselves.
        if (mergedFormats.size() > 1 && pLogicalDevice->supportsMutableFormat) {
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            pLogicalSwapchain->swapchainMutableFormat = true;
        }

        // Allocate our new format list struct at the end, after all formats are added.
        // This prevents dangling pointers if the vector reallocates during addFormat().
        auto formatListCopy = std::make_unique<uint8_t[]>(sizeof(VkImageFormatListCreateInfo));
        VkImageFormatListCreateInfo* newFormatList =
            reinterpret_cast<VkImageFormatListCreateInfo*>(formatListCopy.get());
        newFormatList->sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        newFormatList->pNext           = nullptr;
        newFormatList->viewFormatCount = static_cast<uint32_t>(mergedFormats.size());
        newFormatList->pViewFormats    = mergedFormats.data();
        *newPNextTail                  = reinterpret_cast<VkBaseOutStructure*>(newFormatList);
        pNextCopies.push_back(std::move(formatListCopy));

        // Move the temporary vectors into the swapchain object so they live as long as the swapchain.
        // E.g. validation layer caches the VkImageFormatListCreateInfo pointer for the VkImage.
        pLogicalSwapchain->mergedViewFormats = std::move(mergedFormats);
        pLogicalSwapchain->pNextChainCopies  = std::move(pNextCopies);

        // After the move, the vector storage is owned by pLogicalSwapchain.
        // Update the format list pointer to reference the surviving vector data.
        newFormatList->pViewFormats = pLogicalSwapchain->mergedViewFormats.data();

        modifiedCreateInfo.pNext = newPNextHead;

        // Store the actual usage flags the images will be created with, so that
        // fake image tracker correctly stripsSTORAGE_BIT for SRGB views on real swapchain images.
        pLogicalSwapchain->actualImageUsage = modifiedCreateInfo.imageUsage;

        VkResult result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);

        Logger::info(
            "CreateSwapchainKHR RESULT=" + std::to_string(static_cast<int>(result))
            + " format=" + std::to_string(modifiedCreateInfo.imageFormat)
            + " colorSpace=" + std::to_string(modifiedCreateInfo.imageColorSpace)
            + " extent=" + std::to_string(modifiedCreateInfo.imageExtent.width) + "x"
            + std::to_string(modifiedCreateInfo.imageExtent.height)
            + " imageCount=" + std::to_string(modifiedCreateInfo.minImageCount) + " usage=0x"
            + std::to_string(modifiedCreateInfo.imageUsage)
            + " presentMode=" + std::to_string(modifiedCreateInfo.presentMode));

        if (result == VK_SUCCESS) {
            swapchainMap[*pSwapchain] = pLogicalSwapchain;
        } else {
            Logger::err("CreateSwapchainKHR FAILED with VkResult=" + std::to_string(static_cast<int>(result)));
        }
        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(
        VkDevice device, VkSwapchainKHR swapchain, uint32_t* pCount, VkImage* pSwapchainImages)
    {
        std::scoped_lock l(globalLock);
        Logger::trace(
            "GetSwapchainImagesKHR ENTERED pCount=" + std::to_string(pCount ? *pCount : 0)
            + " pSwapchainImages=" + std::to_string(pSwapchainImages ? 1 : 0));
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        if (pSwapchainImages == nullptr) {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

        // If the images got already requested once, return them again instead of creating new images
        if (pLogicalSwapchain->fakeImages.size()) {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, nullptr);
        pLogicalSwapchain->images.resize(pLogicalSwapchain->imageCount);
        pLogicalDevice->vkd.GetSwapchainImagesKHR(
            device, swapchain, &pLogicalSwapchain->imageCount, pLogicalSwapchain->images.data());

        // Track real swapchain images so SRGB views created by the game get STORAGE_BIT stripped. Must use actualImageUsage
        // (which includes STORAGE_BIT if we added it) rather than the game's original request, otherwise the tracker won't know to strip it.
        for (uint32_t i = 0; i < pLogicalSwapchain->imageCount; i++) {
            registerFakeImageUsage(pLogicalSwapchain->images[i], pLogicalSwapchain->actualImageUsage);
        }

        // Passthrough mode: return real images directly, skip fake image allocation entirely
        Logger::trace(
            "GetSwapchainImagesKHR: passthroughActive=" + std::to_string(pLogicalSwapchain->passthroughActive)
            + " returning " + (pLogicalSwapchain->passthroughActive ? "real" : "fake") + " images");
        if (pLogicalSwapchain->passthroughActive) {
            // Guard: if overlay already exists for this swapchain, just return the real images. Games often call vkGetSwapchainImagesKHR twice (count query + data query).
            if (g_overlayManager.hasOverlay(swapchain)) {
                *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
                std::memcpy(pSwapchainImages, pLogicalSwapchain->images.data(), sizeof(VkImage) * (*pCount));
                return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
            }

            // Free stale command buffers that reference the now destroyed fake images
            if (!pLogicalSwapchain->commandBuffersEffect.empty()) {
                pLogicalDevice->vkd.FreeCommandBuffers(
                    pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersEffect.size(),
                    pLogicalSwapchain->commandBuffersEffect.data());
                pLogicalSwapchain->commandBuffersEffect.clear();
            }
            if (!pLogicalSwapchain->commandBuffersNoEffect.empty()) {
                pLogicalDevice->vkd.FreeCommandBuffers(
                    pLogicalDevice->device, pLogicalDevice->commandPool,
                    pLogicalSwapchain->commandBuffersNoEffect.size(), pLogicalSwapchain->commandBuffersNoEffect.data());
                pLogicalSwapchain->commandBuffersNoEffect.clear();
            }
            pLogicalSwapchain->fakeImages.clear();
            pLogicalSwapchain->effects.clear();
            pLogicalSwapchain->computePasses.clear();
            pLogicalSwapchain->defaultTransfer.reset();
            pLogicalSwapchain->defaultHdrEffect.reset();
            pLogicalSwapchain->passthroughEligible = true;

            if (pLogicalSwapchain->semaphores.empty()) {
                pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
            }

            // Init overlay (renders on top of real images)
            VkFormat overlayFormat = convertToUNORM(pLogicalSwapchain->destFormat);
            g_overlayManager.initOverlay(pLogicalDevice, pLogicalSwapchain, swapchain, overlayFormat, pConfig.get());

            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->images.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        uint32_t totalEffectCount = calculateTotalEffectCount(pConfig.get(), pLogicalSwapchain);

        // Ping pong cap at 2 slices (game input + 1 working buffer) regardless of chain length
        uint32_t requiredSlices = 0;
        if (totalEffectCount == 0)
            requiredSlices = 1;
        else if (totalEffectCount == 1)
            requiredSlices = pLogicalDevice->supportsMutableFormat ? 1 : 2;
        else
            requiredSlices = 2;

        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * requiredSlices;

        pLogicalSwapchain->fakeImages = createFakeSwapchainImages(
            pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, pLogicalSwapchain->fakeImageMemory,
            pLogicalSwapchain->mergedViewFormats);

        Logger::debug("created fake swapchain images");

        // Fake images are already registered with their actual usage (including STORAGE_BIT) inside createFakeSwapchainImages.
        // Don't register again with actualImageUsage here as that overwrites the correct usage with the real swapchain's usage,
        // Which has STORAGE_BIT stripped for SRGB. This would break the SRGB view restriction in vkBasalt_CreateImageView.

        buildEffectChain(pLogicalDevice, pLogicalSwapchain, swapchain, pConfig.get(), g_overlayManager);
        Logger::trace("vkGetSwapchainImagesKHR");

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        std::shared_ptr<LogicalDevice> pLogicalDevice;
        // Only copy the swapchains we actually present (typically 1-3), not the entire map
        std::vector<std::pair<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>>> localSwapchains;
        {
            std::unique_lock<std::mutex> l(globalLock);
            if (processHotkeysAndReloads(pConfig, swapchainMap, g_overlayManager)) {
                LogicalDevice* pDev = deviceMap[GetKey(queue)].get();
                l.unlock(); // Release lock before blocking GPU work to prevent deadlock with CreateSwapchainKHR
                return pDev->vkd.QueuePresentKHR(queue, pPresentInfo);
            }

            // Bypass VRAM Reclaim Timer
            bool currentlyEnabled         = g_effectsEnabled.load();
            static bool previouslyEnabled = true;
            if (previouslyEnabled && !currentlyEnabled) {
                g_effectsDisabledTime           = std::chrono::steady_clock::now();
                g_effectsDisabledTimerActive    = true;
                g_vramReclaimedForCurrentBypass = false;
            } else if (!previouslyEnabled && currentlyEnabled) {
                g_effectsDisabledTimerActive = false;
                // Immediately exit passthrough so the next rebuild creates fake images
                for (auto& [sc, lsc] : swapchainMap) {
                    lsc->passthroughActive      = false;
                    lsc->passthroughTimerActive = false;
                }
                if (g_vramReclaimedForCurrentBypass) {
                    // Pool was shrunk, must grow it back for the full effect chain
                    for (auto& [sc, lsc] : swapchainMap) {
                        lsc->forceSwapchainRebuild = true;
                    }
                }
                g_vramReclaimedForCurrentBypass = false;
            }
            previouslyEnabled = currentlyEnabled;

            if (g_effectsDisabledTimerActive && !g_vramReclaimedForCurrentBypass) {
                auto elapsed = std::chrono::steady_clock::now() - g_effectsDisabledTime;
                if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 15) {
                    g_vramReclaimedForCurrentBypass = true;

                    // Check if we can go straight to passthrough (0 MB) or if we must keep 1 slice for HDR
                    for (auto& [sc, lsc] : swapchainMap) {
                        bool hdrNeeded = isHdrOutputNeeded(pConfig.get(), lsc.get());
                        if (!hdrNeeded) {
                            lsc->passthroughActive      = true;
                            lsc->passthroughTimerActive = false;
                        } else {
                            lsc->passthroughTimerActive = false;
                            lsc->passthroughActive      = false;
                        }
                        lsc->forceSwapchainRebuild = true;
                    }
                }
            }

            // Passthrough Mode Timer (Per swapchain)
            for (auto& [sc, lsc] : swapchainMap) {
                if (lsc->passthroughEligible && !lsc->passthroughActive) {
                    if (!lsc->passthroughTimerActive) {
                        lsc->passthroughTimerStart  = std::chrono::steady_clock::now();
                        lsc->passthroughTimerActive = true;
                    }
                    auto elapsed = std::chrono::steady_clock::now() - lsc->passthroughTimerStart;
                    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 15) {
                        lsc->passthroughActive      = true;
                        lsc->passthroughTimerActive = false;
                        lsc->forceSwapchainRebuild  = true;
                    }
                } else if (!lsc->passthroughEligible) {
                    lsc->passthroughTimerActive = false;
                    // Don't undo passthrough if the bypass timer just activated it.
                    if (lsc->passthroughActive && !g_vramReclaimedForCurrentBypass) {
                        lsc->passthroughActive     = false;
                        lsc->forceSwapchainRebuild = true;
                    }
                }
            }

            pLogicalDevice = deviceMap[GetKey(queue)];
            localSwapchains.reserve(pPresentInfo->swapchainCount);
            for (unsigned int i = 0; i < pPresentInfo->swapchainCount; i++) {
                auto it = swapchainMap.find(pPresentInfo->pSwapchains[i]);
                if (it != swapchainMap.end()) {
                    localSwapchains.emplace_back(it->first, it->second);
                }
            }
        }

        if (!pLogicalDevice) return VK_ERROR_DEVICE_LOST;

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        ensureWaylandRegistryBound();
#endif

        // Stack allocated for the common case (up to 3 swapchains) with safe headroom at 8. Heap fallback for safety.
        VkSemaphore presentSemStack[8];
        std::vector<VkSemaphore> presentSemHeap;
        VkSemaphore* presentSemaphores = presentSemStack;
        uint32_t presentSemCount       = 0;
        if (pPresentInfo->swapchainCount > 8) {
            presentSemHeap.resize(pPresentInfo->swapchainCount);
            presentSemaphores = presentSemHeap.data();
        }

        VkPipelineStageFlags waitStagesStack[8];
        std::vector<VkPipelineStageFlags> waitStagesHeap;
        VkPipelineStageFlags* waitStages = waitStagesStack;
        if (pPresentInfo->waitSemaphoreCount > 8) {
            waitStagesHeap.resize(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            waitStages = waitStagesHeap.data();
        } else {
            for (uint32_t wi = 0; wi < pPresentInfo->waitSemaphoreCount; wi++)
                waitStages[wi] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        bool forceOutOfDate = false;

        for (unsigned int i = 0; i < (*pPresentInfo).swapchainCount; i++) {
            uint32_t index                      = (*pPresentInfo).pImageIndices[i];
            VkSwapchainKHR swapchain            = (*pPresentInfo).pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = nullptr;
            for (auto& [sc, lsc] : localSwapchains) {
                if (sc == swapchain) {
                    pLogicalSwapchain = lsc.get();
                    break;
                }
            }
            if (!pLogicalSwapchain) continue;

            // Passthrough mode: no effect chain, just forward semaphore for overlay
            if (pLogicalSwapchain->passthroughActive) {
                // Handle forced rebuild (e.g., user toggled effects back on, or passthrough just activated)
                if (pLogicalSwapchain->forceSwapchainRebuild) {
                    forceOutOfDate                           = true;
                    pLogicalSwapchain->forceSwapchainRebuild = false;
                    // Still present something valid: forward the semaphore
                    VkSubmitInfo forwardSubmit         = {};
                    forwardSubmit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    forwardSubmit.waitSemaphoreCount   = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
                    forwardSubmit.pWaitSemaphores      = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
                    forwardSubmit.pWaitDstStageMask    = i == 0 ? waitStages : nullptr;
                    forwardSubmit.commandBufferCount   = 0;
                    forwardSubmit.signalSemaphoreCount = 1;
                    forwardSubmit.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);
                    pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &forwardSubmit, VK_NULL_HANDLE);
                    presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];
                    continue;
                }

                VkSubmitInfo forwardSubmit         = {};
                forwardSubmit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                forwardSubmit.waitSemaphoreCount   = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
                forwardSubmit.pWaitSemaphores      = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
                forwardSubmit.pWaitDstStageMask    = i == 0 ? waitStages : nullptr;
                forwardSubmit.commandBufferCount   = 0; // Just semaphore forwarding
                forwardSubmit.signalSemaphoreCount = 1;
                forwardSubmit.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);
                pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &forwardSubmit, VK_NULL_HANDLE);
                presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];

                // Overlay still renders on top of the game's direct render
                if (g_overlayManager.renderOverlay(pLogicalDevice.get(), pLogicalSwapchain, swapchain, index)) {
                    presentSemaphores[presentSemCount - 1] = g_overlayManager.getOverlaySemaphore(swapchain, index);
                }
                continue;
            }

            // If the effect chain grew dynamically submit the fallback and signal OUT_OF_DATE
            if (pLogicalSwapchain->forceSwapchainRebuild) {
                forceOutOfDate                           = true;
                pLogicalSwapchain->forceSwapchainRebuild = false;

                VkSubmitInfo submitInfo         = {};
                submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.waitSemaphoreCount   = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
                submitInfo.pWaitSemaphores      = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
                submitInfo.pWaitDstStageMask    = i == 0 ? waitStages : nullptr;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);

                // Coming from passthrough: no command buffers exist, just forward the semaphore
                if (pLogicalSwapchain->commandBuffersNoEffect.empty()
                    || index >= pLogicalSwapchain->commandBuffersNoEffect.size()) {
                    submitInfo.commandBufferCount = 0;
                } else {
                    submitInfo.commandBufferCount = 1;
                    submitInfo.pCommandBuffers    = &(pLogicalSwapchain->commandBuffersNoEffect[index]);
                }

                pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
                presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];
                continue;
            }

            {
                std::scoped_lock lock(pLogicalSwapchain->effectMutex);
                // Update effects and compute passes even when main effects are bypassed,
                // HDR related effects (NitCalibration, HdrDebug) and FrameAnalyzer still need their
                // UBOs and state updated per frame if they are part of the active "effects bypassed" chain.
                for (auto& effect : pLogicalSwapchain->effects) {
                    effect->updateEffect();
                }
                for (auto& pass : pLogicalSwapchain->computePasses) {
                    pass->updatePass();
                }
                // Use the correct NitCalibrationEffect based on bypass state.
                // Main chain's effect reads from the last processed slice (correct when effects are enabled).
                // Bypassed effect chain's effect reads from slice 0.
                if (g_effectsEnabled.load() && pLogicalSwapchain->nitCalibrationEffect) {
                    pLogicalSwapchain->nitCalibrationEffect->updateHdrMetadata(swapchain);
                } else if (!g_effectsEnabled.load()) {
                    if (pLogicalSwapchain->defaultHdrDebugEffect) {
                        pLogicalSwapchain->defaultHdrDebugEffect->updateEffect();
                    }
                    if (pLogicalSwapchain->defaultHdrEffect) {
                        pLogicalSwapchain->defaultHdrEffect->updateHdrMetadata(swapchain);
                    }
                }
            }

            VkSubmitInfo submitInfo;
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext              = nullptr;
            submitInfo.waitSemaphoreCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
            submitInfo.pWaitSemaphores    = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
            submitInfo.pWaitDstStageMask  = i == 0 ? waitStages : nullptr;
            // Guard: fall back to commandBuffersNoEffect if commandBuffersEffect is empty (e.g., after rebuildFallbackChain allocated but didn't record it)
            bool useEffectChain           = g_effectsEnabled.load() && !pLogicalSwapchain->commandBuffersEffect.empty()
                                            && index < pLogicalSwapchain->commandBuffersEffect.size();
            bool useNoEffectChain         = !useEffectChain && !pLogicalSwapchain->commandBuffersNoEffect.empty()
                                            && index < pLogicalSwapchain->commandBuffersNoEffect.size();
            submitInfo.commandBufferCount = (useEffectChain || useNoEffectChain) ? 1 : 0;
            submitInfo.pCommandBuffers    = useEffectChain     ? &(pLogicalSwapchain->commandBuffersEffect[index])
                                            : useNoEffectChain ? &(pLogicalSwapchain->commandBuffersNoEffect[index])
                                                               : nullptr;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);

            presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];

            VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);

            if (vr != VK_SUCCESS) {
                return vr;
            }

            // ImGui use return value to decide semaphore swap not isOverlayOpen() as the overlay may close itself during rendering.
            if (g_overlayManager.renderOverlay(pLogicalDevice.get(), pLogicalSwapchain, swapchain, index)) {
                presentSemaphores[presentSemCount - 1] = g_overlayManager.getOverlaySemaphore(swapchain, index);
            }

            // Screenshot capture (async submits GPU copy, writes file next frame)
            if (g_triggerScreenshot.load()) {
                g_triggerScreenshot = false;
                bool beforeAfter    = pConfig->getOption<bool>("screenshotBeforeAfter", false);
                auto path           = pConfig->getOption<std::string>("screenshotPath", "");
                auto fmt            = pConfig->getOption<std::string>("screenshotFormat", "png");
                int quality         = pConfig->getOption<int>("screenshotQuality", 95);
                float whitePoint    = pConfig->getOption<float>("sdrWhitePointNits", 203.0f);
                captureScreenshot(
                    pLogicalDevice.get(), pLogicalSwapchain, index, beforeAfter, path, fmt, quality, whitePoint);
            }
        }

        // Process any pending screenshot from the previous frame
        processPendingScreenshot();
        VkPresentInfoKHR presentInfo   = *pPresentInfo;
        presentInfo.waitSemaphoreCount = presentSemCount;
        presentInfo.pWaitSemaphores    = presentSemaphores;

        VkResult result = pLogicalDevice->vkd.QueuePresentKHR(queue, &presentInfo);

        // If any swapchain requested a rebuild, force OUT_OF_DATE so the game engine handles it cleanly
        if (forceOutOfDate) {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }

        return result;
    }

    VKAPI_ATTR void VKAPI_CALL
    vkBasalt_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
    {
        if (!surface) return;
        std::scoped_lock l(globalLock);

        // Clean up platform surface tracking to prevent memory leaks and stale handle reuse
        g_surfaceMap.erase(surface);

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        if (dispatchTable.DestroySurfaceKHR) {
            dispatchTable.DestroySurfaceKHR(instance, surface, pAllocator);
        }
    }

    VKAPI_ATTR void VKAPI_CALL
    vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain) return;
        std::scoped_lock l(globalLock);

        auto it = swapchainMap.find(swapchain);
        if (it == swapchainMap.end()) {
            // Swapchain not tracked by us, just pass through
            LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
            pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
            return;
        }

        // we need to delete the infos of the oldswapchain
        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        // Drop fake and real image usage records before the handles go away
        unregisterFakeImageUsages(it->second->fakeImages);
        unregisterFakeImageUsages(it->second->images);

        // Wait for the GPU to finish using our internal resources before destroying them.
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        // Cleanup rebuild fence before destroying the swapchain
        if (it->second->rebuildFence != VK_NULL_HANDLE) {
            pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, it->second->rebuildFence, nullptr);
            it->second->rebuildFence = VK_NULL_HANDLE;
        }
        it->second->destroy();
        swapchainMap.erase(it);

        // Cleanup Overlay Resources
        g_overlayManager.destroyOverlay(pLogicalDevice, swapchain);

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    static void rerecordAllCommandBuffers(
        LogicalDevice* pLogicalDevice, VkImage depthImage, VkImageView depthImageView, VkFormat depthFormat)
    {
        for (auto& it_swap : swapchainMap) {
            LogicalSwapchain* pLogicalSwapchain = it_swap.second.get();
            if (pLogicalSwapchain->pLogicalDevice != pLogicalDevice) continue;
            if (pLogicalSwapchain->effects.empty() || pLogicalSwapchain->commandBuffersEffect.empty()) continue;

            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersEffect.size(),
                pLogicalSwapchain->commandBuffersEffect.data());

            pLogicalSwapchain->commandBuffersEffect.clear();
            pLogicalSwapchain->commandBuffersEffect =
                allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
            Logger::debug("allocated CommandBuffers for swapchain " + convertToString(it_swap.first));

            writeCommandBuffers(
                pLogicalDevice, pLogicalSwapchain, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat,
                pLogicalSwapchain->commandBuffersEffect);
            Logger::debug("wrote CommandBuffers");
        }
    }

    static void bindTrackedDepthImage(LogicalDevice* pLogicalDevice, VkImage image)
    {
        auto it = std::ranges::find(pLogicalDevice->depthImages, image);
        if (it == pLogicalDevice->depthImages.end()) return;

        size_t index = std::distance(pLogicalDevice->depthImages.begin(), it);

        if (pLogicalDevice->depthImageViews.size() <= index) {
            pLogicalDevice->depthImageViews.resize(index + 1, VK_NULL_HANDLE);
        }

        if (pLogicalDevice->depthImageViews[index] != VK_NULL_HANDLE) return;

        VkFormat depthFormat = pLogicalDevice->depthFormats[index];
        VkImageView depthImageView =
            createImageViews(pLogicalDevice, depthFormat, {image}, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT)[0];
        pLogicalDevice->depthImageViews[index] = depthImageView;

        // Only re-record for the first valid depth buffer
        bool isFirstDepthBuffer = true;
        for (size_t i = 0; i < index; ++i) {
            if (pLogicalDevice->depthImageViews[i] != VK_NULL_HANDLE) {
                isFirstDepthBuffer = false;
                break;
            }
        }
        if (isFirstDepthBuffer) {
            rerecordAllCommandBuffers(pLogicalDevice, image, depthImageView, depthFormat);
        }
    }

    static void untrackDepthImage(LogicalDevice* pLogicalDevice, VkImage image)
    {
        for (uint32_t i = 0; i < pLogicalDevice->depthImages.size(); i++) {
            if (pLogicalDevice->depthImages[i] != image) continue;

            pLogicalDevice->depthImages.erase(pLogicalDevice->depthImages.begin() + i);

            if (i < pLogicalDevice->depthImageViews.size()) {
                if (pLogicalDevice->depthImageViews[i] != VK_NULL_HANDLE) {
                    pLogicalDevice->vkd.DestroyImageView(
                        pLogicalDevice->device, pLogicalDevice->depthImageViews[i], nullptr);
                }
                pLogicalDevice->depthImageViews.erase(pLogicalDevice->depthImageViews.begin() + i);
            }
            pLogicalDevice->depthFormats.erase(pLogicalDevice->depthFormats.begin() + i);

            VkImageView depthImageView =
                pLogicalDevice->depthImageViews.empty() ? VK_NULL_HANDLE : pLogicalDevice->depthImageViews[0];
            VkImage depthImage = pLogicalDevice->depthImages.empty() ? VK_NULL_HANDLE : pLogicalDevice->depthImages[0];
            VkFormat depthFormat =
                pLogicalDevice->depthFormats.empty() ? VK_FORMAT_UNDEFINED : pLogicalDevice->depthFormats[0];

            rerecordAllCommandBuffers(pLogicalDevice, depthImage, depthImageView, depthFormat);
            return;
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImageView(
        VkDevice device,
        const VkImageViewCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkImageView* pView)
    {
        std::scoped_lock l(globalLock);
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        if (pCreateInfo) {
            VkImageUsageFlags restrictedUsage = 0;
            bool needsRestriction             = false;

            // Only restrict if the image is tracked (i.e., we might have added STORAGE_BIT to it). If it's untracked, it's a regular game image.
            // Therefore we can't safely guess the usage subset without risking VUID-VkImageViewCreateInfo-pNext-02662 or 08931.
            if (getStorageStrippedViewUsage(
                    pLogicalDevice, pCreateInfo->image, pCreateInfo->format, &restrictedUsage)) {
                needsRestriction = true;
            }

            if (needsRestriction) {
                // Patch the game's usage restriction without dropping any other pNext struct.
                // Use VkBaseOutStructure because we temporarily relink the chain, then restore it.
                VkBaseOutStructure* prev = nullptr;
                const void* pNext        = pCreateInfo->pNext;

                while (pNext) {
                    auto* base = const_cast<VkBaseOutStructure*>(reinterpret_cast<const VkBaseOutStructure*>(pNext));

                    if (base->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO) {
                        VkImageViewUsageCreateInfo patched = *reinterpret_cast<const VkImageViewUsageCreateInfo*>(base);
                        patched.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
                        patched.pNext = base->pNext;

                        // Save the original predecessor target, which is base itself, not base->pNext.
                        VkBaseOutStructure* savedPrevNext = base;
                        if (prev) prev->pNext = reinterpret_cast<VkBaseOutStructure*>(&patched);

                        VkImageViewCreateInfo modifiedCreateInfo = *pCreateInfo;
                        if (!prev) modifiedCreateInfo.pNext = &patched;

                        VkResult r =
                            pLogicalDevice->vkd.CreateImageView(device, &modifiedCreateInfo, pAllocator, pView);
                        if (prev) prev->pNext = savedPrevNext;
                        return r;
                    }

                    if (base->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_2_CREATE_INFO_KHR) {
                        VkImageViewUsage2CreateInfoKHR patched =
                            *reinterpret_cast<const VkImageViewUsage2CreateInfoKHR*>(base);
                        patched.usage &= ~VK_IMAGE_USAGE_2_STORAGE_BIT_KHR;
                        patched.pNext = base->pNext;

                        VkBaseOutStructure* savedPrevNext = base;
                        if (prev) prev->pNext = reinterpret_cast<VkBaseOutStructure*>(&patched);

                        VkImageViewCreateInfo modifiedCreateInfo = *pCreateInfo;
                        if (!prev) modifiedCreateInfo.pNext = &patched;

                        VkResult r =
                            pLogicalDevice->vkd.CreateImageView(device, &modifiedCreateInfo, pAllocator, pView);
                        if (prev) prev->pNext = savedPrevNext;
                        return r;
                    }

                    prev  = base;
                    pNext = base->pNext;
                }

                // No existing structure found, safely prepend ours.
                VkImageViewUsageCreateInfo usageRestriction = {};
                usageRestriction.sType                      = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
                usageRestriction.pNext                      = pCreateInfo->pNext;
                usageRestriction.usage                      = restrictedUsage;

                VkImageViewCreateInfo modifiedCreateInfo = *pCreateInfo;
                modifiedCreateInfo.pNext                 = &usageRestriction;
                return pLogicalDevice->vkd.CreateImageView(device, &modifiedCreateInfo, pAllocator, pView);
            }
        }

        return pLogicalDevice->vkd.CreateImageView(device, pCreateInfo, pAllocator, pView);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImage(
        VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
    {
        std::scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (isDepthFormat(pCreateInfo->format) && pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT
            && ((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            Logger::debug("detected depth image with format: " + convertToString(pCreateInfo->format));
            Logger::debug(std::to_string(pCreateInfo->extent.width) + "x" + std::to_string(pCreateInfo->extent.height));
            Logger::debug(
                std::to_string(
                    (pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

            VkImageCreateInfo modifiedCreateInfo = *pCreateInfo;
            modifiedCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkResult result = pLogicalDevice->vkd.CreateImage(device, &modifiedCreateInfo, pAllocator, pImage);
            pLogicalDevice->depthImages.push_back(*pImage);
            pLogicalDevice->depthFormats.push_back(pCreateInfo->format);

            return result;
        } else {
            return pLogicalDevice->vkd.CreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL
    vkBasalt_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
    {
        std::scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkResult result = pLogicalDevice->vkd.BindImageMemory(device, image, memory, memoryOffset);
        bindTrackedDepthImage(pLogicalDevice, image);
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL
    vkBasalt_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
    {
        if (!image) return;
        std::scoped_lock l(globalLock);
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        unregisterFakeImageUsage(image);
        untrackDepthImage(pLogicalDevice, image);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, pAllocator);
    }

    // Enumeration function
    VkResult VKAPI_CALL
    vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (pPropertyCount) *pPropertyCount = 1;

        if (pProperties) {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer");
            pProperties->implementationVersion = 1;
            pProperties->specVersion           = VK_MAKE_VERSION(1, 2, 0);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(
        VkPhysicalDevice /*physicalDevice*/, uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(
        const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* /*pProperties*/)
    {
        if (pLayerName == nullptr || std::strcmp(pLayerName, VKBASALT_NAME) != 0) {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        // don't expose any extensions
        if (pPropertyCount) {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(
        VkPhysicalDevice physicalDevice,
        const char* pLayerName,
        uint32_t* pPropertyCount,
        VkExtensionProperties* pProperties)
    {
        // pass through any queries that aren't to us
        if (pLayerName == nullptr || std::strcmp(pLayerName, VKBASALT_NAME) != 0) {
            if (physicalDevice == VK_NULL_HANDLE) {
                return VK_SUCCESS;
            }

            std::scoped_lock l(globalLock);
            return instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        // don't expose any extensions
        if (pPropertyCount) {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }
} // namespace vkBasalt

extern "C"
{ // these are the entry points for the layer, so they need to be c-linkeable

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName);
    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define GETPROCADDR(func) \
    if (!std::strcmp(pName, "vk" #func)) return (PFN_vkVoidFunction) & vkBasalt::vkBasalt_##func;
    /*
    Return our funktions for the funktions we want to intercept
    the macro takes the name and returns our vkBasalt_##func, if the name is equal
    */

    // vkGetDeviceProcAddr needs to behave like vkGetInstanceProcAddr thanks to some games

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#define VKBASALT_INTERCEPT_WAYLAND GETPROCADDR(CreateWaylandSurfaceKHR);
#else
#define VKBASALT_INTERCEPT_WAYLAND
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
#define VKBASALT_INTERCEPT_XLIB GETPROCADDR(CreateXlibSurfaceKHR);
#else
#define VKBASALT_INTERCEPT_XLIB
#endif

#define INTERCEPT_CALLS \
    /* instance chain functions we intercept */ \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction) & vkBasalt_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
    VKBASALT_INTERCEPT_WAYLAND \
    VKBASALT_INTERCEPT_XLIB \
\
    /* device chain functions we intercept*/ \
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction) & vkBasalt_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
    GETPROCADDR(CreateSwapchainKHR); \
    GETPROCADDR(GetSwapchainImagesKHR); \
    GETPROCADDR(QueuePresentKHR); \
    GETPROCADDR(DestroySwapchainKHR); \
    GETPROCADDR(DestroySurfaceKHR); \
    GETPROCADDR(CreateImageView); \
\
    if (vkBasalt::pConfig->getOption<std::string>("depthCapture", "off") == "on") { \
        GETPROCADDR(CreateImage); \
        GETPROCADDR(DestroyImage); \
        GETPROCADDR(BindImageMemory); \
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        vkBasalt::ensureConfig();

        INTERCEPT_CALLS

        {
            std::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::deviceMap[vkBasalt::GetKey(device)]->vkd.GetDeviceProcAddr(device, pName);
        }
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        vkBasalt::ensureConfig();

        INTERCEPT_CALLS

        {
            std::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::instanceDispatchMap[vkBasalt::GetKey(instance)].GetInstanceProcAddr(instance, pName);
        }
    }

} // extern "C"
