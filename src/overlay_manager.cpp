#include "overlay_manager.hpp"
#include "imgui_overlay.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "command_buffer.hpp"
#include "format.hpp"
#include "logger.hpp"
#include "util.hpp"
#include <unistd.h>

namespace vkBasalt {
    extern pid_t g_layer_init_pid;
    // Define the global reload triggers
    std::atomic<bool> g_effectsEnabled{true};
    std::atomic<bool> g_triggerHotReload{false};
    std::atomic<bool> g_triggerSoftReload{false};
    std::atomic<bool> g_triggerPreviewReload{false};
    std::atomic<bool> g_triggerRevertReload{false};

    OverlayManager::OverlayManager() {}
    OverlayManager::~OverlayManager() {}

    void OverlayManager::initOverlay(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                                     VkSwapchainKHR swapchain, VkFormat format, Config* pConfig) {
        // Skip in forked child processes (Vulkan Loader invalidates handles)
        if (getpid() != g_layer_init_pid) {
            Logger::debug("Fork detected! Skipping ImGui initialization in child process.");
            return;
        }

        auto overlay = std::make_shared<ImGuiOverlay>(pDevice, pSwapchain, pConfig);
        overlay->initImGui(format);
        m_overlayMap[swapchain] = overlay;
        m_commandBuffersMap[swapchain] = allocateCommandBuffer(pDevice, pSwapchain->imageCount);
        m_semaphoresMap[swapchain] = createSemaphores(pDevice, pSwapchain->imageCount);
    }

    bool OverlayManager::renderOverlay(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                                       VkSwapchainKHR swapchain, uint32_t imageIndex) {
        auto overlayIt = m_overlayMap.find(swapchain);
        if (overlayIt == m_overlayMap.end() || !overlayIt->second || !overlayIt->second->isOverlayOpen()) {
            return false;
        }

        Logger::debug("Overlay is OPEN. Recording and submitting overlay command buffer...");
        VkCommandBuffer overlayCmdBuf = m_commandBuffersMap[swapchain][imageIndex];
        pDevice->vkd.ResetCommandBuffer(overlayCmdBuf, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pDevice->vkd.BeginCommandBuffer(overlayCmdBuf, &beginInfo);

        VkFormat unormFormat = convertToUNORM(pSwapchain->format);
        overlayIt->second->processFrame(overlayCmdBuf, imageIndex, unormFormat,
                                        pSwapchain->imageExtent.width, pSwapchain->imageExtent.height);

        pDevice->vkd.EndCommandBuffer(overlayCmdBuf);

        VkSemaphore effectSem = pSwapchain->semaphores[imageIndex];
        VkSemaphore overlaySem = m_semaphoresMap[swapchain][imageIndex];
        VkPipelineStageFlags overlayWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo overlaySubmitInfo = {};
        overlaySubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        overlaySubmitInfo.waitSemaphoreCount = 1;
        overlaySubmitInfo.pWaitSemaphores = &effectSem;
        overlaySubmitInfo.pWaitDstStageMask = &overlayWaitStage;
        overlaySubmitInfo.commandBufferCount = 1;
        overlaySubmitInfo.pCommandBuffers = &overlayCmdBuf;
        overlaySubmitInfo.signalSemaphoreCount = 1;
        overlaySubmitInfo.pSignalSemaphores = &overlaySem;

        pDevice->vkd.QueueSubmit(pDevice->queue, 1, &overlaySubmitInfo, VK_NULL_HANDLE);
        return true;
    }

    void OverlayManager::destroyOverlay(LogicalDevice* pDevice, VkSwapchainKHR swapchain) {
        m_overlayMap.erase(swapchain);

        if (m_commandBuffersMap.count(swapchain)) {
            auto& cmds = m_commandBuffersMap[swapchain];
            if (!cmds.empty()) {
                pDevice->vkd.FreeCommandBuffers(pDevice->device, pDevice->commandPool, cmds.size(), cmds.data());
            }
            m_commandBuffersMap.erase(swapchain);
        }

        if (m_semaphoresMap.count(swapchain)) {
            for (auto sem : m_semaphoresMap[swapchain]) {
                pDevice->vkd.DestroySemaphore(pDevice->device, sem, nullptr);
            }
            m_semaphoresMap.erase(swapchain);
        }
    }

    void OverlayManager::updateAllOverlays(Config* pConfig, bool reinit) {
        for (auto& pair : m_overlayMap) {
            if (pair.second) {
                pair.second->updateConfig(pConfig);
                if (reinit) pair.second->reinitImGui();
            }
        }
    }

    void OverlayManager::closeAllOverlays() {
        for (auto& pair : m_overlayMap) {
            if (pair.second && pair.second->isOverlayOpen()) {
                pair.second->toggleOverlay();
            }
        }
    }

    void OverlayManager::toggleAllOverlays() {
        Logger::debug("INSERT KEY DETECTED! Toggling overlay. overlayMap size: " + std::to_string(m_overlayMap.size()));
        for (auto& pair : m_overlayMap) {
            if (pair.second) {
                pair.second->toggleOverlay();
                Logger::debug(std::string("Overlay state is now: ") + (pair.second->isOverlayOpen() ? "OPEN" : "CLOSED"));
            }
        }
    }

    bool OverlayManager::anyBinding() const {
        for (auto& pair : m_overlayMap) {
            if (pair.second && pair.second->isOverlayOpen() && pair.second->isBindingKeys()) {
                return true;
            }
        }
        return false;
    }

    VkSemaphore OverlayManager::getOverlaySemaphore(VkSwapchainKHR swapchain, uint32_t index) const {
        auto it = m_semaphoresMap.find(swapchain);
        if (it != m_semaphoresMap.end() && index < it->second.size()) {
            return it->second[index];
        }
        return VK_NULL_HANDLE;
    }

    std::shared_ptr<ImGuiOverlay> OverlayManager::getOverlay(VkSwapchainKHR swapchain) const {
        auto it = m_overlayMap.find(swapchain);
        return (it != m_overlayMap.end()) ? it->second : nullptr;
    }
} // namespace vkBasalt
