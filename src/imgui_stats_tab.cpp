#include "imgui_overlay.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vulkan/vulkan_core.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "game_detect.hpp"
#include "frame_analyzer.hpp"
#include "overlay_manager.hpp"
#include "config.hpp"
#include "format.hpp"
#include "keyboard_input.hpp"

namespace vkBasalt {

    static const char* vkFormatToString(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_B8G8R8A8_UNORM:                       return "B8G8R8A8_UNORM";
            case VK_FORMAT_B8G8R8A8_SRGB:                        return "B8G8R8A8_SRGB";
            case VK_FORMAT_R8G8B8A8_UNORM:                       return "R8G8B8A8_UNORM";
            case VK_FORMAT_R8G8B8A8_SRGB:                        return "R8G8B8A8_SRGB";
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:             return "A2B10G10R10_UNORM (HDR10)";
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:             return "A2R10G10B10_UNORM (HDR10)";
            case VK_FORMAT_R16G16B16A16_SFLOAT:                  return "R16G16B16A16_SFLOAT (scRGB)";
            case VK_FORMAT_R16G16B16_SFLOAT:                     return "R16G16B16_SFLOAT (scRGB)";
            case VK_FORMAT_R32G32B32A32_SFLOAT:                  return "R32G32B32A32_SFLOAT";
            default:                                             return "Other / Unknown";
        }
    }

    static const char* vkColorSpaceToString(VkColorSpaceKHR colorSpace)
    {
        switch (colorSpace)
        {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:              return "sRGB Non-Linear";
            case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:        return "Display P3 Non-Linear";
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:        return "Extended sRGB Linear (scRGB)";
            case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:           return "Display P3 Linear";
            case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:            return "DCI-P3 Non-Linear";
            case VK_COLOR_SPACE_BT709_LINEAR_EXT:                return "BT.709 Linear";
            case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:             return "BT.709 Non-Linear";
            case VK_COLOR_SPACE_BT2020_LINEAR_EXT:               return "BT.2020 Linear";
            case VK_COLOR_SPACE_HDR10_ST2084_EXT:                return "HDR10 ST2084 (PQ)";
            case VK_COLOR_SPACE_DOLBYVISION_EXT:                 return "Dolby Vision";
            case VK_COLOR_SPACE_HDR10_HLG_EXT:                   return "HDR10 HLG";
            case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT:             return "AdobeRGB Linear";
            case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:          return "AdobeRGB Non-Linear";
            case VK_COLOR_SPACE_PASS_THROUGH_EXT:                return "Pass-Through";
            case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:     return "Extended sRGB Non-Linear";
            default:                                             return "Other / Unknown";
        }
    }

    static const char* vkPresentModeToString(VkPresentModeKHR presentMode)
    {
        switch (presentMode)
        {
            case VK_PRESENT_MODE_IMMEDIATE_KHR:                  return "Immediate (No V-Sync)";
            case VK_PRESENT_MODE_MAILBOX_KHR:                    return "Mailbox (Fast Sync)";
            case VK_PRESENT_MODE_FIFO_KHR:                       return "FIFO (V-Sync)";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:               return "FIFO Relaxed (Adaptive V-Sync)";
            case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:      return "Shared Demand Refresh";
            case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:  return "Shared Continuous Refresh";
            default:                                             return "Unknown";
        }
    }

    void ImGuiOverlay::drawStatsTab() {
        ImGui::Text("System & Display Statistics");
        ImGui::Separator();
        ImGui::Spacing();

        auto statRow = [](const char* label, const char* value) {
            ImGui::Text("%s", label);
            ImGui::SameLine(280);
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", value ? value : "Unknown");
        };

        if (ImGui::CollapsingHeader("Application", ImGuiTreeNodeFlags_DefaultOpen)) {
            statRow("Game", getGameDisplayName().c_str());

            char exePath[4096] = {0};
            ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
            std::string exeName = "Unknown";
            if (len > 0) {
                exeName = std::string(exePath, (size_t)len);
                size_t slash = exeName.find_last_of('/');
                if (slash != std::string::npos) exeName = exeName.substr(slash + 1);
            }
            statRow("Process", exeName.c_str());
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Display & Swapchain", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                char resBuf[32];
                snprintf(resBuf, sizeof(resBuf), "%u x %u", m_pSwapchain->imageExtent.width, m_pSwapchain->imageExtent.height);
                statRow("Resolution", resBuf);

                statRow("Pixel Format", vkFormatToString(m_pSwapchain->format));
                statRow("Color Space", vkColorSpaceToString(m_pSwapchain->colorSpace));

                char imgBuf[16];
                snprintf(imgBuf, sizeof(imgBuf), "%u", m_pSwapchain->imageCount);
                statRow("Image Count", imgBuf);

                statRow("Present Mode", vkPresentModeToString(m_pSwapchain->swapchainCreateInfo.presentMode));
                statRow("Mutable Format", m_pDevice->supportsMutableFormat ? "Supported" : "Not Supported");
            } else {
                ImGui::TextDisabled("No active swapchain.");
            }
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Hardware & Vulkan", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pDevice) {
                statRow("GPU", m_pDevice->physicalDeviceProperties.deviceName);

                uint32_t apiVer = m_pDevice->physicalDeviceProperties.apiVersion;
                char apiBuf[32];
                snprintf(apiBuf, sizeof(apiBuf), "%u.%u.%u",
                    VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer), VK_VERSION_PATCH(apiVer));
                statRow("Vulkan API", apiBuf);

                uint32_t drvVer = m_pDevice->physicalDeviceProperties.driverVersion;
                char drvBuf[32];
                snprintf(drvBuf, sizeof(drvBuf), "%u.%u.%u",
                    VK_VERSION_MAJOR(drvVer), VK_VERSION_MINOR(drvVer), VK_VERSION_PATCH(drvVer));
                statRow("Driver Version", drvBuf);

                uint64_t totalVRAM = 0;
                for (uint32_t i = 0; i < m_pDevice->memoryProperties.memoryHeapCount; i++) {
                    if (m_pDevice->memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                        totalVRAM += m_pDevice->memoryProperties.memoryHeaps[i].size;
                    }
                }
                char vramBuf[32];
                snprintf(vramBuf, sizeof(vramBuf), "%.2f GB", (float)totalVRAM / (1024.0f * 1024.0f * 1024.0f));
                statRow("Total VRAM", vramBuf);
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Effect Chain", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                statRow("Effects Active", g_effectsEnabled.load() ? "YES" : "BYPASSED");
                char effectCountBuf[16];
                snprintf(effectCountBuf, sizeof(effectCountBuf), "%zu", m_pSwapchain->effects.size());
                statRow("Effect Count", effectCountBuf);

                // List active effect names
                std::string effectNames;
                for (size_t i = 0; i < m_pSwapchain->effects.size(); i++) {
                    if (i > 0) effectNames += " → ";
                    effectNames += m_pSwapchain->effects[i]->getName();
                }
                statRow("Chain Order", effectNames.empty() ? "None" : effectNames.c_str());

                char fakeImgBuf[64];
                size_t fakeCount = m_pSwapchain->fakeImages.size();
                uint32_t bpp = getBytesPerPixel(m_pSwapchain->sourceFormat);
                float poolMB = (float)(fakeCount * m_pSwapchain->imageExtent.width * m_pSwapchain->imageExtent.height * bpp) / (1024.0f * 1024.0f);
                snprintf(fakeImgBuf, sizeof(fakeImgBuf), "%zu images (%.1f MB)", fakeCount, poolMB);
                statRow("Fake Image Pool", fakeImgBuf);

                statRow("Auto HDR", m_pSwapchain->autoHdrActive ? "Active (SDR→HDR)" : "Inactive");

                char computeBuf[16];
                snprintf(computeBuf, sizeof(computeBuf), "%zu", m_pSwapchain->computePasses.size());
                statRow("Compute Passes", computeBuf);
            } else {
                ImGui::TextDisabled("No active swapchain.");
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pConfig) {
                statRow("Global Config", m_pConfig->getGlobalPath().c_str());
                statRow("Per-Game Config", m_pConfig->getGamePath().c_str());
                statRow("Per-Game Overrides", m_pConfig->hasPerGameOverrides() ? "YES" : "NO");
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Overlay & Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* waylandDisplay = getenv("WAYLAND_DISPLAY");
            const char* x11Display = getenv("DISPLAY");
            
            std::string displayServer;
            // Use the actual confirmed input backend instead of guessing from env vars (Proton sets both WAYLAND_DISPLAY and DISPLAY even for native Wayland games)
            if (isWaylandBackend()) {
                displayServer = "Wayland";
            } else if (x11Display) {
                if (waylandDisplay) {
                    displayServer = "XWayland";
                } else {
                    displayServer = "X11";
                }
            } else {
                displayServer = "Unknown";
            }
            statRow("Display Server", displayServer.c_str());

            char uiBuf[16], curBuf[16], fontBuf[16];
            snprintf(uiBuf, sizeof(uiBuf), "%.2f", m_uiScale);
            snprintf(curBuf, sizeof(curBuf), "%.2f", m_cursorScale);
            snprintf(fontBuf, sizeof(fontBuf), "%.2f", m_fontScale);

            statRow("UI Scale", uiBuf);
            statRow("Cursor Scale", curBuf);
            statRow("Font Scale", fontBuf);
        }
    }
} // namespace vkBasalt
