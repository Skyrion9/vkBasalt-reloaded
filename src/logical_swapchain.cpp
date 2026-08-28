#include "logical_swapchain.hpp"
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    void LogicalSwapchain::destroy()
    {
        computePasses.clear();
        effects.clear();
        defaultTransfer.reset();

        if (!commandBuffersEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersEffect.size(), commandBuffersEffect.data());
            commandBuffersEffect.clear();
        }
        if (!commandBuffersNoEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersNoEffect.size(), commandBuffersNoEffect.data());
            commandBuffersNoEffect.clear();
        }

        if (fakeImageMemory != VK_NULL_HANDLE) {
            pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, fakeImageMemory, nullptr);
            fakeImageMemory = VK_NULL_HANDLE;
        }
        for (auto& img : fakeImages) {
            if (img != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, img, nullptr);
        }
        fakeImages.clear();

        for (auto& sem : semaphores) {
            if (sem != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, sem, nullptr);
        }
        semaphores.clear();

        imageCount = 0;
    }
} // namespace vkBasalt
