#include "vkdispatch.hpp"
#include <X11/X.h>
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
#include <vulkan/vulkan_xlib.h>
#undef None
#undef Bool
#undef Status
#undef Always
#undef Success
#undef True
#undef False

#include <mutex>
#include <vector>
#include <unordered_map>
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


#define VKBASALT_NAME "VK_LAYER_VKBASALT_post_processing"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    std::shared_ptr<Config> pConfig = nullptr;
    Logger Logger::s_instance;
    pid_t g_layer_init_pid = 0;

    // layer book-keeping information, to store dispatch tables by key
    std::unordered_map<void*, InstanceDispatch>                           instanceDispatchMap;
    std::unordered_map<void*, VkInstance>                                 instanceMap;
    std::unordered_map<void*, uint32_t>                                   instanceVersionMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>>             deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    std::mutex globalLock;
    static OverlayManager g_overlayManager;

#ifdef _GCC_
    using scoped_lock __attribute__((unused)) = std::lock_guard<std::mutex>;
#else
    using scoped_lock = std::lock_guard<std::mutex>;
#endif

    static void ensureConfig()
    {
        if (pConfig == nullptr)
        {
            scoped_lock l(globalLock);
            if (pConfig == nullptr)
            {
                pConfig = std::make_shared<Config>();
            }
        }
    }

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkInstance*                  pInstance)
    {
        if (g_layer_init_pid == 0) {
            g_layer_init_pid = getpid();
        }
        VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }

        Logger::trace("vkCreateInstance");

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance) gpa(VK_NULL_HANDLE, "vkCreateInstance");

        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
        VkApplicationInfo    appInfo;
        if (modifiedCreateInfo.pApplicationInfo)
        {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_1)
            {
                appInfo.apiVersion = VK_API_VERSION_1_1;
            }
        }
        else
        {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_1;
        }

        modifiedCreateInfo.pApplicationInfo = &appInfo;
        VkResult ret                        = createFunc(&modifiedCreateInfo, pAllocator, pInstance);

        // fetch our own dispatch table for the functions we need, into the next layer
        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        // store the table by key
        {
            scoped_lock l(globalLock);
            instanceDispatchMap[GetKey(*pInstance)] = dispatchTable;
            instanceMap[GetKey(*pInstance)]         = *pInstance;
            instanceVersionMap[GetKey(*pInstance)]  = modifiedCreateInfo.pApplicationInfo->apiVersion;
        }

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;
        scoped_lock l(globalLock);
        Logger::trace("vkDestroyInstance");
        // Don't shutdownWaylandInput() here. Games with launchers (Naraka/NEAC) destroy the launcher instance and create a new one for
        // the main game. Destroying Wayland input here kills hotkeys for the main game instance.
        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        dispatchTable.DestroyInstance(instance, pAllocator);
        instanceDispatchMap.erase(GetKey(instance));
        instanceMap.erase(GetKey(instance));
        instanceVersionMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(VkPhysicalDevice             physicalDevice,
                                               const VkDeviceCreateInfo*    pCreateInfo,
                                               const VkAllocationCallbacks* pAllocator,
                                               VkDevice*                    pDevice)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice) gipa(VK_NULL_HANDLE, "vkCreateDevice");

        // check and activate extentions
        uint32_t extensionCount = 0;

        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat = false;
        for (VkExtensionProperties properties : extensionProperties)
        {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format"))
            {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
                break;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo       modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount)
        {
            enabledExtensionNames = std::vector<const char*>(modifiedCreateInfo.ppEnabledExtensionNames,
                                                             modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        if (supportsMutableFormat)
        {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
        }
        if (deviceProps.apiVersion < VK_API_VERSION_1_2 || instanceVersionMap[GetKey(physicalDevice)] < VK_API_VERSION_1_2)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        // Active needed Features
        VkPhysicalDeviceFeatures deviceFeatures = {};
        if (modifiedCreateInfo.pEnabledFeatures)
        {
            deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
        }
        deviceFeatures.shaderImageGatherExtended = VK_TRUE;
        modifiedCreateInfo.pEnabledFeatures      = &deviceFeatures;

        VkResult ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

        if (ret != VK_SUCCESS)
            return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice(new LogicalDevice());
        pLogicalDevice->vki                   = instanceDispatchMap[GetKey(physicalDevice)];
        pLogicalDevice->device                = *pDevice;
        pLogicalDevice->physicalDevice        = physicalDevice;
        pLogicalDevice->instance              = instanceMap[GetKey(physicalDevice)];
        pLogicalDevice->queue                 = VK_NULL_HANDLE;
        pLogicalDevice->queueFamilyIndex      = 0;
        pLogicalDevice->commandPool           = VK_NULL_HANDLE;
        pLogicalDevice->supportsMutableFormat = supportsMutableFormat;

        fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);

        uint32_t count;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, queueProperties.data());
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue)
            Logger::err("Did not find a graphics queue!");
        // Cache memory properties before any fork so ImGui can use them without calling vkGetPhysicalDeviceMemoryProperties through the loader
        pLogicalDevice->vki.GetPhysicalDeviceMemoryProperties(pLogicalDevice->physicalDevice, &pLogicalDevice->memoryProperties);

        // Initialize pipeline cache (loaded from disk, saved on device destroy)
        pLogicalDevice->pipelineCachePath = getPipelineCachePath(pLogicalDevice->physicalDevice, pLogicalDevice->vki);
        VkPipelineCacheCreateInfo cacheInfo = {};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        std::vector<uint8_t> cacheData = loadPipelineCacheData(pLogicalDevice->pipelineCachePath);
        if (!cacheData.empty()) {
            cacheInfo.initialDataSize = cacheData.size();
            cacheInfo.pInitialData = cacheData.data();
        }
        pLogicalDevice->vkd.CreatePipelineCache(pLogicalDevice->device, &cacheInfo, nullptr, &pLogicalDevice->pipelineCache);
        deviceMap[GetKey(*pDevice)] = pLogicalDevice;
        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        // Save pipeline cache to disk before destroying
        savePipelineCacheData(device, pLogicalDevice->vkd, pLogicalDevice->pipelineCache, pLogicalDevice->pipelineCachePath);

        if (pLogicalDevice->pipelineCache != VK_NULL_HANDLE)
        {
            pLogicalDevice->vkd.DestroyPipelineCache(device, pLogicalDevice->pipelineCache, nullptr);
            pLogicalDevice->pipelineCache = VK_NULL_HANDLE;
        }

        if (pLogicalDevice->commandPool != VK_NULL_HANDLE)
        {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, pAllocator);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        deviceMap.erase(GetKey(device));
    }

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateWaylandSurfaceKHR(
        VkInstance                                  instance,
        const VkWaylandSurfaceCreateInfoKHR*        pCreateInfo,
        const VkAllocationCallbacks*                pAllocator,
        VkSurfaceKHR*                               pSurface)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkCreateWaylandSurfaceKHR");

        // Grab display pointer
        if (pCreateInfo && pCreateInfo->display)
        {
            setInputBackend(true); // Confirm Wayland backend
            initWaylandInput((void*)pCreateInfo->display, (void*)pCreateInfo->surface);
        }

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        PFN_vkCreateWaylandSurfaceKHR fpCreateWaylandSurfaceKHR = 
            (PFN_vkCreateWaylandSurfaceKHR)dispatchTable.GetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR");
            
        if (fpCreateWaylandSurfaceKHR)
        {
            return fpCreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
        }
        
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXlibSurfaceKHR(
    VkInstance                                  instance,
    const VkXlibSurfaceCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
    {
    scoped_lock l(globalLock);
    Logger::trace("vkCreateXlibSurfaceKHR");
        // Grab display pointer and window handle for X11 input
        if (pCreateInfo && pCreateInfo->dpy)
        {
            setInputBackend(false); // Confirm X11/XWayland backend
            initX11Input((void*)pCreateInfo->dpy, (void*)(uintptr_t)pCreateInfo->window);
        }
        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];
        PFN_vkCreateXlibSurfaceKHR fpCreateXlibSurfaceKHR = 
            (PFN_vkCreateXlibSurfaceKHR)dispatchTable.GetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");
        if (fpCreateXlibSurfaceKHR)
        {
            return fpCreateXlibSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
        }
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
#endif

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice                        device,
                                                                 const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                                 const VkAllocationCallbacks*    pAllocator,
                                                                 VkSwapchainKHR*                 pSwapchain)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        if (pLogicalDevice->supportsMutableFormat)
        {
            modifiedCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                             | VK_IMAGE_USAGE_SAMPLED_BIT; // we want to use the swapchain images as output of the graphics pipeline
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            // TODO what if the application already uses multiple formats for the swapchain?

            imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
            imageFormatListCreateInfo.pNext           = modifiedCreateInfo.pNext;
            imageFormatListCreateInfo.viewFormatCount = (srgbFormat == unormFormat) ? 1 : 2;
            imageFormatListCreateInfo.pViewFormats    = formats;

            modifiedCreateInfo.pNext = &imageFormatListCreateInfo;
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        Logger::debug("colorSpace " + std::to_string(modifiedCreateInfo.imageColorSpace));

        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain(new LogicalSwapchain());
        pLogicalSwapchain->pLogicalDevice      = pLogicalDevice;
        pLogicalSwapchain->swapchainCreateInfo = *pCreateInfo;
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->format              = modifiedCreateInfo.imageFormat;
        pLogicalSwapchain->colorSpace          = modifiedCreateInfo.imageColorSpace;
        pLogicalSwapchain->imageCount          = 0;

        VkResult result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);

        swapchainMap[*pSwapchain] = pLogicalSwapchain;

        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice       device,
                                                                   VkSwapchainKHR swapchain,
                                                                   uint32_t*      pCount,
                                                                   VkImage*       pSwapchainImages)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        if (pSwapchainImages == nullptr)
        {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

        // If the images got already requested once, return them again instead of creating new images
        if (pLogicalSwapchain->fakeImages.size())
        {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, nullptr);
        pLogicalSwapchain->images.resize(pLogicalSwapchain->imageCount);
        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, pLogicalSwapchain->images.data());

        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});
        // Ping-pong cap at 3 slices (game input + 2 working buffers) regardless of chain length
        uint32_t requiredSlices;
        if (effectStrings.size() == 0) requiredSlices = 1;
        else if (effectStrings.size() == 1) requiredSlices = pLogicalDevice->supportsMutableFormat ? 1 : 2;
        else requiredSlices = 3;
        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * requiredSlices;

        pLogicalSwapchain->fakeImages =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, pLogicalSwapchain->fakeImageMemory);
        Logger::debug("created fake swapchain images");

        buildEffectChain(pLogicalDevice, pLogicalSwapchain, swapchain, pConfig.get(), g_overlayManager);
        Logger::trace("vkGetSwapchainImagesKHR");

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        // Hold lock only for map lookups and hotkey processing. Release before GPU work.
        std::shared_ptr<LogicalDevice> pLogicalDevice;
        // Only copy the swapchains we actually present (typically 1-3), not the entire map
        std::vector<std::pair<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>>> localSwapchains;
        {
            scoped_lock l(globalLock);
            if (processHotkeysAndReloads(pConfig, swapchainMap, g_overlayManager)) {
                LogicalDevice* pDev = deviceMap[GetKey(queue)].get();
                return pDev->vkd.QueuePresentKHR(queue, pPresentInfo);
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
        uint32_t presentSemCount = 0;
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

        for (unsigned int i = 0; i < (*pPresentInfo).swapchainCount; i++)
        {
            uint32_t          index             = (*pPresentInfo).pImageIndices[i];
            VkSwapchainKHR    swapchain         = (*pPresentInfo).pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = nullptr;
            for (auto& [sc, lsc] : localSwapchains) {
                if (sc == swapchain) { pLogicalSwapchain = lsc.get(); break; }
            }
            if (!pLogicalSwapchain) continue;

            // If the effect chain grew dynamically submit the fallback and signal OUT_OF_DATE
            if (pLogicalSwapchain->forceSwapchainRebuild) {
                forceOutOfDate = true;
                pLogicalSwapchain->forceSwapchainRebuild = false;

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.waitSemaphoreCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
                submitInfo.pWaitSemaphores = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
                submitInfo.pWaitDstStageMask = i == 0 ? waitStages : nullptr;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &(pLogicalSwapchain->commandBuffersNoEffect[index]);
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &(pLogicalSwapchain->semaphores[index]);

                pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
                presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];
                continue; 
            }

            if (g_effectsEnabled.load()) {
                std::lock_guard<std::mutex> lock(pLogicalSwapchain->effectMutex);
                for (auto& effect : pLogicalSwapchain->effects)
                {
                    effect->updateEffect();
                }
            }

            VkSubmitInfo submitInfo;
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext              = nullptr;
            submitInfo.waitSemaphoreCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
            submitInfo.pWaitSemaphores    = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
            submitInfo.pWaitDstStageMask  = i == 0 ? waitStages : nullptr;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers =
                g_effectsEnabled.load() ? &(pLogicalSwapchain->commandBuffersEffect[index]) : &(pLogicalSwapchain->commandBuffersNoEffect[index]);
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);

            presentSemaphores[presentSemCount++] = pLogicalSwapchain->semaphores[index];

            VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);

            if (vr != VK_SUCCESS)
            {
                return vr;
            }

            // ImGui use return value to decide semaphore swap not isOverlayOpen() as the overlay may close itself during rendering.
            if (g_overlayManager.renderOverlay(pLogicalDevice.get(), pLogicalSwapchain, swapchain, index)) {
                presentSemaphores[presentSemCount - 1] = g_overlayManager.getOverlaySemaphore(swapchain, index);
            }

            // Screenshot capture (async submits GPU copy, writes file next frame)
            if (g_triggerScreenshot.load()) {
                g_triggerScreenshot = false;
                bool beforeAfter = pConfig->getOption<bool>("screenshotBeforeAfter", false);
                std::string path = pConfig->getOption<std::string>("screenshotPath", "");
                std::string fmt = pConfig->getOption<std::string>("screenshotFormat", "png");
                int quality = pConfig->getOption<int>("screenshotQuality", 95);
                captureScreenshot(pLogicalDevice.get(), pLogicalSwapchain, index, beforeAfter, path, fmt, quality);
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

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain)
            return;

        scoped_lock l(globalLock);
        // we need to delete the infos of the oldswapchain
        
        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));

        // Cleanup rebuild fence before destroying the swapchain
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (swapchainMap[swapchain]->rebuildFence != VK_NULL_HANDLE) {
            pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, swapchainMap[swapchain]->rebuildFence, nullptr);
            swapchainMap[swapchain]->rebuildFence = VK_NULL_HANDLE;
        }

        swapchainMap[swapchain]->destroy();
        swapchainMap.erase(swapchain);

        // Cleanup Overlay Resources
        g_overlayManager.destroyOverlay(pLogicalDevice, swapchain);

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImage(VkDevice                     device,
                                                         const VkImageCreateInfo*     pCreateInfo,
                                                         const VkAllocationCallbacks* pAllocator,
                                                         VkImage*                     pImage)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (isDepthFormat(pCreateInfo->format) && pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT
            && ((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            Logger::debug("detected depth image with format: " + convertToString(pCreateInfo->format));
            Logger::debug(std::to_string(pCreateInfo->extent.width) + "x" + std::to_string(pCreateInfo->extent.height));
            Logger::debug(
                std::to_string((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

            VkImageCreateInfo modifiedCreateInfo = *pCreateInfo;
            modifiedCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkResult result = pLogicalDevice->vkd.CreateImage(device, &modifiedCreateInfo, pAllocator, pImage);
            pLogicalDevice->depthImages.push_back(*pImage);
            pLogicalDevice->depthFormats.push_back(pCreateInfo->format);

            return result;
        }
        else
        {
            return pLogicalDevice->vkd.CreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkResult result = pLogicalDevice->vkd.BindImageMemory(device, image, memory, memoryOffset);
        
        // Fixed: Search for the image in depthImages to handle cases where multiple images are created before binding
        auto it = std::find(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(), image);
        if (it != pLogicalDevice->depthImages.end())
        {
            size_t index = std::distance(pLogicalDevice->depthImages.begin(), it);
            
            // Ensure the view vector is large enough
            if (pLogicalDevice->depthImageViews.size() <= index) {
                pLogicalDevice->depthImageViews.resize(index + 1, VK_NULL_HANDLE);
            }

            // Only create the view if we haven't already
            if (pLogicalDevice->depthImageViews[index] == VK_NULL_HANDLE)
            {
                Logger::debug("before creating depth image view");
                VkFormat depthFormat = pLogicalDevice->depthFormats[index];

                VkImageView depthImageView = createImageViews(pLogicalDevice,
                                                                depthFormat,
                                                                {image},
                                                                VK_IMAGE_VIEW_TYPE_2D,
                                                                VK_IMAGE_ASPECT_DEPTH_BIT)[0];

                Logger::debug("created depth image view");
                pLogicalDevice->depthImageViews[index] = depthImageView;

                // The original code only re-recorded command buffers for the first depth buffer.
                // We preserve this behavior by checking if this is the first valid view.
                bool isFirstDepthBuffer = true;
                for(size_t i = 0; i < index; ++i) {
                    if (pLogicalDevice->depthImageViews[i] != VK_NULL_HANDLE) {
                        isFirstDepthBuffer = false;
                        break;
                    }
                }

                if (isFirstDepthBuffer)
                {
                    for (auto& it_swap : swapchainMap)
                    {
                        LogicalSwapchain* pLogicalSwapchain = it_swap.second.get();
                        if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice && !pLogicalSwapchain->effects.empty())
                        {
                            if (pLogicalSwapchain->commandBuffersEffect.size())
                            {
                                pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device,
                                                                         pLogicalDevice->commandPool,
                                                                         pLogicalSwapchain->commandBuffersEffect.size(),
                                                                         pLogicalSwapchain->commandBuffersEffect.data());
                                pLogicalSwapchain->commandBuffersEffect.clear();
                                pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
                                Logger::debug("allocated CommandBuffers for swapchain " + convertToString(it_swap.first));

                                writeCommandBuffers(
                                    pLogicalDevice, pLogicalSwapchain->effects, image, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);
                                Logger::debug("wrote CommandBuffers");
                            }
                        }
                    }
                }
            }
        }
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
    {
        if (!image)
            return;

        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        for (uint32_t i = 0; i < pLogicalDevice->depthImages.size(); i++)
        {
            if (pLogicalDevice->depthImages[i] == image)
            {
                pLogicalDevice->depthImages.erase(pLogicalDevice->depthImages.begin() + i);
                
                // Fixed: Safer bounds check and null check
                if (i < pLogicalDevice->depthImageViews.size())
                {
                    if (pLogicalDevice->depthImageViews[i] != VK_NULL_HANDLE)
                    {
                        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, pLogicalDevice->depthImageViews[i], nullptr);
                    }
                    pLogicalDevice->depthImageViews.erase(pLogicalDevice->depthImageViews.begin() + i);
                }
                
                pLogicalDevice->depthFormats.erase(pLogicalDevice->depthFormats.begin() + i);

                VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
                VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
                VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;
                
                for (auto& it_swap : swapchainMap)
                {
                    LogicalSwapchain* pLogicalSwapchain = it_swap.second.get();
                    if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice && !pLogicalSwapchain->effects.empty())
                    {
                        if (pLogicalSwapchain->commandBuffersEffect.size())
                        {
                            pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device,
                                                                     pLogicalDevice->commandPool,
                                                                     pLogicalSwapchain->commandBuffersEffect.size(),
                                                                     pLogicalSwapchain->commandBuffersEffect.data());
                            pLogicalSwapchain->commandBuffersEffect.clear();
                            pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
                            Logger::debug("allocated CommandBuffers for swapchain " + convertToString(it_swap.first));

                            writeCommandBuffers(pLogicalDevice,
                                                pLogicalSwapchain->effects,
                                                depthImage,
                                                depthImageView,
                                                depthFormat,
                                                pLogicalSwapchain->commandBuffersEffect);
                            Logger::debug("wrote CommandBuffers");
                        }
                    }
                }
                
                break; 
            }
        }

        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, pAllocator);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    // Enumeration function

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (pPropertyCount)
            *pPropertyCount = 1;

        if (pProperties)
        {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer");
            pProperties->implementationVersion = 1;
            pProperties->specVersion           = VK_MAKE_VERSION(1, 2, 0);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
                                                                 uint32_t*          pPropertyCount,
                                                                 VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                       uint32_t*              pPropertyCount,
                                                                       VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                     const char*            pLayerName,
                                                                     uint32_t*              pPropertyCount,
                                                                     VkExtensionProperties* pProperties)
    {
        // pass through any queries that aren't to us
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            if (physicalDevice == VK_NULL_HANDLE)
            {
                return VK_SUCCESS;
            }

            scoped_lock l(globalLock);
            return instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
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
    if (!std::strcmp(pName, "vk" #func)) \
        return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_##func;
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
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
    VKBASALT_INTERCEPT_WAYLAND \
    VKBASALT_INTERCEPT_XLIB \
\
    /* device chain functions we intercept*/ \
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
    GETPROCADDR(CreateSwapchainKHR); \
    GETPROCADDR(GetSwapchainImagesKHR); \
    GETPROCADDR(QueuePresentKHR); \
    GETPROCADDR(DestroySwapchainKHR); \
\
    if (vkBasalt::pConfig->getOption<std::string>("depthCapture", "off") == "on") \
    { \
        GETPROCADDR(CreateImage); \
        GETPROCADDR(DestroyImage); \
        GETPROCADDR(BindImageMemory); \
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        vkBasalt::ensureConfig();

        INTERCEPT_CALLS

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::deviceMap[vkBasalt::GetKey(device)]->vkd.GetDeviceProcAddr(device, pName);
        }
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        vkBasalt::ensureConfig();

        INTERCEPT_CALLS

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::instanceDispatchMap[vkBasalt::GetKey(instance)].GetInstanceProcAddr(instance, pName);
        }
    }

} // extern "C"
