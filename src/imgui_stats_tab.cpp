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

namespace vkBasalt
{

    void ImGuiOverlay::drawStatsTab()
    {
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

            char exePath[4096]  = {0};
            ssize_t len         = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
            std::string exeName = "Unknown";
            if (len > 0) {
                exeName      = std::string(exePath, static_cast<size_t>(len));
                size_t slash = exeName.find_last_of('/');
                if (slash != std::string::npos) exeName = exeName.substr(slash + 1);
            }
            statRow("Process", exeName.c_str());
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Display & Swapchain", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                char resBuf[32];
                snprintf(
                    resBuf, sizeof(resBuf), "%u x %u", m_pSwapchain->imageExtent.width,
                    m_pSwapchain->imageExtent.height);
                statRow("Resolution", resBuf);

                statRow("Pixel Format", formatName(m_pSwapchain->format));
                statRow("Color Space", colorSpaceName(m_pSwapchain->colorSpace));

                char imgBuf[16];
                snprintf(imgBuf, sizeof(imgBuf), "%u", m_pSwapchain->imageCount);
                statRow("Image Count", imgBuf);

                statRow("Present Mode", presentModeName(m_pSwapchain->swapchainCreateInfo.presentMode));
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
                snprintf(
                    apiBuf, sizeof(apiBuf), "%u.%u.%u", VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer),
                    VK_VERSION_PATCH(apiVer));
                statRow("Vulkan API", apiBuf);

                uint32_t drvVer = m_pDevice->physicalDeviceProperties.driverVersion;
                char drvBuf[32];
                snprintf(
                    drvBuf, sizeof(drvBuf), "%u.%u.%u", VK_VERSION_MAJOR(drvVer), VK_VERSION_MINOR(drvVer),
                    VK_VERSION_PATCH(drvVer));
                statRow("Driver Version", drvBuf);

                uint64_t totalVRAM = 0;
                for (uint32_t i = 0; i < m_pDevice->memoryProperties.memoryHeapCount; i++) {
                    if (m_pDevice->memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                        totalVRAM += m_pDevice->memoryProperties.memoryHeaps[i].size;
                    }
                }
                char vramBuf[32];
                snprintf(
                    vramBuf, sizeof(vramBuf), "%.2f GB", static_cast<float>(totalVRAM) / (1024.0f * 1024.0f * 1024.0f));
                statRow("Total VRAM", vramBuf);
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Effect Chain", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                statRow("Effects Active", g_effectsEnabled.load() ? "YES" : "BYPASSED");

                // Passthrough State
                const char* passthroughState = "Inactive";
                if (m_pSwapchain->passthroughActive) {
                    passthroughState = "Active (0 MB overhead)";
                } else if (m_pSwapchain->passthroughEligible) {
                    passthroughState = "Eligible (Waiting 15s)";
                }
                statRow("Passthrough", passthroughState);

                char effectCountBuf[16];
                snprintf(effectCountBuf, sizeof(effectCountBuf), "%zu", m_pSwapchain->effects.size());
                statRow("Effect Count", effectCountBuf);

                // List active effect names
                std::string effectNames;
                for (size_t i = 0; i < m_pSwapchain->effects.size(); i++) {
                    if (i > 0) effectNames += " -> ";
                    effectNames += m_pSwapchain->effects[i]->getName();
                }
                statRow("Chain Order", effectNames.empty() ? "None" : effectNames.c_str());

                // Allocated Slices
                uint32_t slices =
                    m_pSwapchain->fakeImages.empty() ? 0 : (m_pSwapchain->fakeImages.size() / m_pSwapchain->imageCount);
                char sliceBuf[16];
                snprintf(sliceBuf, sizeof(sliceBuf), "%u", slices);
                statRow("Allocated Slices", sliceBuf);

                // Fake Image Pool (SDR/Source format)
                char fakeImgBuf[64];
                size_t fakeCount = m_pSwapchain->fakeImages.size();
                uint32_t srcBpp  = getBytesPerPixel(m_pSwapchain->sourceFormat);
                float fakePoolMB =
                    static_cast<float>(
                        fakeCount * m_pSwapchain->imageExtent.width * m_pSwapchain->imageExtent.height * srcBpp)
                    / (1024.0f * 1024.0f);
                snprintf(fakeImgBuf, sizeof(fakeImgBuf), "%zu images (%.1f MB)", fakeCount, fakePoolMB);
                statRow("Fake Image Pool", fakeImgBuf);

                // Real Image Pool (HDR/Dest format)
                uint32_t destBpp = getBytesPerPixel(m_pSwapchain->destFormat);
                float realPoolMB = static_cast<float>(
                                       m_pSwapchain->imageCount * m_pSwapchain->imageExtent.width
                                       * m_pSwapchain->imageExtent.height * destBpp)
                                   / (1024.0f * 1024.0f);
                char realImgBuf[64];
                snprintf(realImgBuf, sizeof(realImgBuf), "%u images (%.1f MB)", m_pSwapchain->imageCount, realPoolMB);
                statRow("Real Image Pool", realImgBuf);

                // Auto HDR & Metadata
                if (m_pSwapchain->autoHdrActive) {
                    statRow("Auto HDR", "Active (SDR->HDR)");
                    float targetWhite = m_pConfig ? m_pConfig->getOption<float>("sdrWhitePointNits", 203.0f) : 203.0f;
                    float targetPeak  = m_pConfig ? m_pConfig->getOption<float>("hdrPeakNits", 1000.0f) : 1000.0f;
                    char hdrBuf[64];
                    snprintf(hdrBuf, sizeof(hdrBuf), "White: %.0f nits | Peak: %.0f nits", targetWhite, targetPeak);
                    statRow("HDR Targets", hdrBuf);
                } else {
                    statRow("Auto HDR", "Inactive");
                }

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
            const char* x11Display     = getenv("DISPLAY");

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
