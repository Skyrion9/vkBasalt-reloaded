#pragma once

#include "layer_globals.hpp"
#include <unordered_map>
#include <vector>
#include <memory>

namespace vkBasalt
{
    struct LogicalDevice;
    struct LogicalSwapchain;
    class Config;
    class ImGuiOverlay;

    class OverlayManager
    {
    public:
        OverlayManager();
        ~OverlayManager();

        // Initialize overlay for a newly created swapchain. Called from initializeSwapchainEffects when swapchain != VK_NULL_HANDLE.
        void initOverlay(
            LogicalDevice* pDevice,
            LogicalSwapchain* pSwapchain,
            VkSwapchainKHR swapchain,
            VkFormat format,
            Config* pConfig);

        // Render overlay into the command buffer if open. Called from QueuePresentKHR after effect submission.
        // Returns true if the overlay was actually rendered this frame
        bool renderOverlay(
            LogicalDevice* pDevice, LogicalSwapchain* pSwapchain, VkSwapchainKHR swapchain, uint32_t imageIndex);

        // Cleanup overlay resources when swapchain is destroyed.
        void destroyOverlay(LogicalDevice* pDevice, VkSwapchainKHR swapchain);
        bool hasOverlay(VkSwapchainKHR swapchain) const { return m_overlayMap.count(swapchain) > 0; }

        // Update overlay config pointers after config reload.
        void updateAllOverlays(Config* pConfig, bool reinit);

        // Close all open overlays.
        void closeAllOverlays();

        // Toggle overlay for all swapchains.
        void toggleAllOverlays();

        // Check if any overlay is in key-binding mode.
        bool anyBinding() const;

        // Get the overlay semaphore for a swapchain/index (used to update present semaphore)
        VkSemaphore getOverlaySemaphore(VkSwapchainKHR swapchain, uint32_t index) const;

        // Get overlay for a specific swapchain (may return nullptr).
        std::shared_ptr<ImGuiOverlay> getOverlay(VkSwapchainKHR swapchain) const;

    private:
        bool m_lastOverlayOpenState = false;
        int m_lastActiveTab         = 0;

        std::unordered_map<VkSwapchainKHR, std::shared_ptr<ImGuiOverlay>> m_overlayMap;
        std::unordered_map<VkSwapchainKHR, std::vector<VkCommandBuffer>> m_commandBuffersMap;
        std::unordered_map<VkSwapchainKHR, std::vector<VkSemaphore>> m_semaphoresMap;
    };
} // namespace vkBasalt
