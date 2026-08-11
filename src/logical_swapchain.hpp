#ifndef LOGICAL_SWAPCHAIN_HPP_INCLUDED
#define LOGICAL_SWAPCHAIN_HPP_INCLUDED

#include<vector>
#include<fstream>
#include<string>
#include<iostream>
#include<vector>
#include<memory>
#include<mutex>
#include<atomic>

#include"effect.hpp"

#include"vulkan_include.hpp"

#include"logical_device.hpp"

namespace vkBasalt
{

    // for each swapchain, we have the Images and the other stuff we need to execute the compute shader
    struct LogicalSwapchain
    {
        LogicalDevice*                       pLogicalDevice;
        VkSwapchainCreateInfoKHR             swapchainCreateInfo;
        VkExtent2D                           imageExtent;
        VkFormat                             format;
        VkColorSpaceKHR                      colorSpace;
        uint32_t                             imageCount;
        std::vector<VkImage>                 images;
        std::vector<VkImage>                 fakeImages;
        std::vector<VkCommandBuffer>         commandBuffersEffect;
        std::vector<VkCommandBuffer>         commandBuffersNoEffect;
        std::vector<VkSemaphore>             semaphores;
        std::vector<std::shared_ptr<Effect>> effects;
        std::shared_ptr<Effect>              defaultTransfer;
        VkDeviceMemory                       fakeImageMemory;

        // flag to force the game to recreate the swapchain if the effect chain grows dynamically.
        // prevents device loss by letting the game engine cleanly release its cached VkImage handles.
        bool                                 forceSwapchainRebuild = false;

        // Thread safe runtime state management for ImGui
        mutable std::mutex                   effectMutex;
        std::atomic<bool>                    needsRecreation{false};

        // Fence based deferred rebuild (avoids QueueWaitIdle deadlock in QueuePresentKHR)
        VkFence                              rebuildFence = VK_NULL_HANDLE;
        bool                                 pendingRebuild = false;

        void destroy();
    };
} // namespace vkBasalt

#endif // LOGICAL_SWAPCHAIN_HPP_INCLUDED
