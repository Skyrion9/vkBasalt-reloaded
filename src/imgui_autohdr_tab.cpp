#include "imgui_overlay.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <string>
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "format.hpp"
#include "overlay_manager.hpp"
#include "config.hpp"
#include "frame_analyzer.hpp"
#include "hdr_detect.hpp"

namespace vkBasalt {

    static const char* formatName(VkFormat format) {
        switch (format) {
            case VK_FORMAT_B8G8R8A8_UNORM:           return "B8G8R8A8 (8-bit SDR)";
            case VK_FORMAT_B8G8R8A8_SRGB:            return "B8G8R8A8 sRGB (8-bit SDR)";
            case VK_FORMAT_R8G8B8A8_UNORM:           return "R8G8B8A8 (8-bit SDR)";
            case VK_FORMAT_R8G8B8A8_SRGB:            return "R8G8B8A8 sRGB (8-bit SDR)";
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10 (10-bit HDR)";
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "A2R10G10B10 (10-bit HDR)";
            case VK_FORMAT_R16G16B16A16_SFLOAT:      return "RGBA16F (scRGB Linear)";
            case VK_FORMAT_R16G16B16_SFLOAT:         return "RGB16F (scRGB Linear)";
            case VK_FORMAT_R32G32B32A32_SFLOAT:      return "RGBA32F (Linear)";
            case VK_FORMAT_R32G32B32_SFLOAT:         return "RGB32F (Linear)"; // Added
            default:                                 return "Unknown";
        }
    }

    static const char* colorSpaceName(VkColorSpaceKHR colorSpace) {
        switch (colorSpace) {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:          return "sRGB";
            case VK_COLOR_SPACE_HDR10_ST2084_EXT:            return "HDR10 PQ (ST 2084)";
            case VK_COLOR_SPACE_HDR10_HLG_EXT:               return "HDR10 HLG";
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:    return "scRGB Linear";
            case VK_COLOR_SPACE_BT2020_LINEAR_EXT:           return "BT.2020 Linear";
            case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:       return "Display P3 Linear";
            case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:    return "Display P3 (sRGB Gamma)"; // Added
            case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:        return "DCI-P3";                 // Added
            case VK_COLOR_SPACE_BT709_LINEAR_EXT:            return "BT.709 Linear";          // Added
            case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:         return "BT.709";                 // Added
            case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT:         return "AdobeRGB Linear";        // Added
            case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:      return "AdobeRGB";               // Added
            case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return "Extended sRGB";          // Added
            case VK_COLOR_SPACE_DOLBYVISION_EXT:             return "Dolby Vision";
            case VK_COLOR_SPACE_PASS_THROUGH_EXT:            return "Pass-Through";
            default:                                         return "Unknown";
        }
    }

    void ImGuiOverlay::drawAutoHdrTab() {
        ImGui::Text("Auto HDR & Display Calibration");
        ImGui::Separator();
        ImGui::Spacing();

        bool gameIsHDR = false;
        if (m_pSwapchain) {
            ColorSpaceMode srcCsm = getColorSpaceMode(m_pSwapchain->sourceFormat, m_pSwapchain->sourceColorSpace);
            gameIsHDR = (srcCsm != ColorSpaceMode::SDR_SRGB);
        }

        if (ImGui::CollapsingHeader("Current Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                if (gameIsHDR) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Game is outputting HDR natively");
                } else if (m_pSwapchain->autoHdrActive) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Auto HDR is ACTIVE");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Game is outputting SDR");
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Source:  %s / %s", formatName(m_pSwapchain->sourceFormat), colorSpaceName(m_pSwapchain->sourceColorSpace));
                ImGui::TextDisabled("Output:  %s / %s", formatName(m_pSwapchain->destFormat), colorSpaceName(m_pSwapchain->destColorSpace));
                ImGui::TextDisabled("Mutable: %s | Auto HDR Active: %s",
                    m_pSwapchain->pLogicalDevice->supportsMutableFormat ? "YES" : "NO",
                    m_pSwapchain->autoHdrActive ? "YES" : "NO");
            } else {
                ImGui::TextDisabled("No active swapchain.");
            }
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Auto HDR (SDR to HDR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Automatically converts SDR games to HDR output, requires HDR display.");
            ImGui::Spacing();
            ImGui::BeginDisabled(gameIsHDR);
            bool autoHdr = m_pConfig->getOption<bool>("autoHdr", true);
            if (ImGui::Checkbox("Enable Auto HDR", &autoHdr)) {
                m_pConfig->setOption("autoHdr", autoHdr ? "on" : "off");
                m_pConfig->savePerGame();
                if (m_pSwapchain) m_pSwapchain->forceSwapchainRebuild = true;
            }
            ImGui::EndDisabled();
            if (gameIsHDR) ImGui::TextDisabled("Disabled: Game is already outputting HDR.");
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("HDR Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("White point and peak brightness scaling for Auto HDR. Optionally usable for native HDR games.");
            ImGui::Spacing();
            bool isNativeHdr = gameIsHDR;
            bool isAutoHdrActive = m_pSwapchain && m_pSwapchain->autoHdrActive;
            bool hdrCalibConfig = m_pConfig->getOption<bool>("hdrCalibration", false);
            bool displayChecked = isAutoHdrActive || hdrCalibConfig;
            bool isDisabled = isAutoHdrActive || (!isNativeHdr && !isAutoHdrActive);

            ImGui::BeginDisabled(isDisabled);
            if (ImGui::Checkbox("Enable HDR Calibration", &displayChecked)) {
                m_pConfig->setOption("hdrCalibration", displayChecked ? "on" : "off");
                m_pConfig->savePerGame();
                g_triggerSoftReload = true;
            }
            ImGui::EndDisabled();
            
            if (isAutoHdrActive) {
                ImGui::TextDisabled("Forced ON: Auto HDR uses calibration to map SDR to your display.");
            } else if (!isNativeHdr) {
                ImGui::TextDisabled("Disabled: Requires Auto HDR or native HDR game.");
            }
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Display Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Set these to match your display's capabilities.");
            ImGui::Spacing();

            // Per game calibration toggle, as some native HDR games might need different numbers depending on implementation.
            bool perGameCalib = m_pConfig->hasPerGameOption("sdrWhitePointNits") ||
                                m_pConfig->hasPerGameOption("hdrPeakNits") ||
                                m_pConfig->hasPerGameOption("hdrToneMapper");
            if (ImGui::Checkbox("Only change calibration for this game", &perGameCalib)) {
                if (perGameCalib) {
                    // Lock in the current global values to the per game config immediately so the checkbox state sticks
                    m_pConfig->setOption("sdrWhitePointNits", doubleToConfigString(m_pConfig->getOption<float>("sdrWhitePointNits", 203.0f)));
                    m_pConfig->setOption("hdrPeakNits", doubleToConfigString(m_pConfig->getOption<float>("hdrPeakNits", 1000.0f)));
                    m_pConfig->setOption("hdrToneMapper", m_pConfig->getOption<std::string>("hdrToneMapper", "quality"));
                    m_pConfig->savePerGame();
                    g_triggerSoftReload = true;
                } else {
                    // Reverting to global, remove per game overrides
                    m_pConfig->removePerGameOption("sdrWhitePointNits");
                    m_pConfig->removePerGameOption("hdrPeakNits");
                    m_pConfig->removePerGameOption("hdrToneMapper");
                    m_pConfig->savePerGame();
                    g_triggerSoftReload = true;
                }
            }
            if (perGameCalib) {
                ImGui::TextDisabled("Calibration changes will be saved to this game's config only.");
            }
            ImGui::Spacing();

            DisplayHdrInfo detected = detectDisplayHdrCalibration();
            std::string sourceStr = "Fallback Defaults";
            if (detected.source == "kde") sourceStr = "KDE Plasma";
            
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "System Detection: %s", sourceStr.c_str());
            ImGui::TextDisabled("Detected Peak: %.0f nits | White: %.0f nits", 
                                detected.peakBrightnessNits, detected.sdrWhitePointNits);
            ImGui::Spacing();

            std::string toneMapperMode = m_pConfig->getOption<std::string>("hdrToneMapper", "quality");
            int toneMapperIndex = (toneMapperMode == "fast" || toneMapperMode == "igpu") ? 1 : 0;
            const char* toneMapperItems[] = { "Quality (more precise)", "Fast (iGPU / performance)" };
            
            ImGui::PushItemWidth(250);
            if (ImGui::Combo("Processing Mode", &toneMapperIndex, toneMapperItems, 2)) {
                if (perGameCalib) {
                    m_pConfig->setOption("hdrToneMapper", toneMapperIndex == 1 ? "fast" : "quality");
                    m_pConfig->savePerGame();
                } else {
                    m_pConfig->setGlobalOption("hdrToneMapper", toneMapperIndex == 1 ? "fast" : "quality");
                    m_pConfig->saveGlobal();
                }
                g_triggerSoftReload = true;
            }
            ImGui::PopItemWidth();

            if (toneMapperIndex == 0) {
                ImGui::TextDisabled("Quality: Rational curve, Hunt effect, achromatic clipping.");
            } else {
                ImGui::TextDisabled("Fast: Polynomial sRGB, simpler curve, MaxRGB clamp.");
            }
            ImGui::Spacing();

            float sdrWhite = m_pConfig->getOption<float>("sdrWhitePointNits", 203.0f);
            ImGui::PushItemWidth(250);
            if (ImGui::SliderFloat("SDR White Point (nits)", &sdrWhite, 80.0f, 400.0f, "%.0f")) {
                if (perGameCalib) {
                    m_pConfig->setOption("sdrWhitePointNits", doubleToConfigString(sdrWhite));
                } else {
                    m_pConfig->setGlobalOption("sdrWhitePointNits", doubleToConfigString(sdrWhite));
                }
                m_hasUnsavedChanges = true;
                m_previewDirty = true;
                m_lastChangeTime = ImGui::GetTime();
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("100 (ref), 203 (HDR10 standard), 80-120 (dim rooms)");

            ImGui::Spacing();

            float peakNits = m_pConfig->getOption<float>("hdrPeakNits", 1000.0f);
            ImGui::PushItemWidth(250);
            if (ImGui::SliderFloat("Peak Brightness (nits)", &peakNits, 200.0f, 4000.0f, "%.0f")) {
                if (perGameCalib) {
                    m_pConfig->setOption("hdrPeakNits", doubleToConfigString(peakNits));
                } else {
                    m_pConfig->setGlobalOption("hdrPeakNits", doubleToConfigString(peakNits));
                }
                m_hasUnsavedChanges = true;
                m_previewDirty = true;
                m_lastChangeTime = ImGui::GetTime();
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Clamps HDR highlights to your display's measured peak.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Scopes", ImGuiTreeNodeFlags_DefaultOpen)) {
            FrameAnalyzer* analyzer = nullptr;
            if (m_pSwapchain) {
                for (auto& pass : m_pSwapchain->computePasses) {
                    if (pass->getName() == "frame_analyzer") {
                        analyzer = static_cast<FrameAnalyzer*>(pass.get());
                        break;
                    }
                }
            }

            if (analyzer) {
                bool enabled = analyzer->isEnabled();
                if (ImGui::Checkbox("Enable Scopes (GPU)", &enabled)) {
                    analyzer->setEnabled(enabled);
                    g_triggerSoftReload = true;
                }

                if (enabled) {
                    // Register textures again if the FrameAnalyzer was recreated (swapchain rebuild, soft reload, etc.)
                    if (!m_scopeTexturesRegistered || (void*)analyzer != m_lastAnalyzerPtr) {
                        if (m_scopeTexturesRegistered) {
                            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(uintptr_t)m_scopeTextureIDs[0]);
                            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(uintptr_t)m_scopeTextureIDs[1]);
                            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(uintptr_t)m_scopeTextureIDs[2]);
                        }
                        m_scopeTextureIDs[0] = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(),
                            analyzer->getScopeImageView(FrameAnalyzer::HISTOGRAM),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTextureIDs[1] = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(),
                            analyzer->getScopeImageView(FrameAnalyzer::WAVEFORM),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTextureIDs[2] = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(),
                            analyzer->getScopeImageView(FrameAnalyzer::VECTORSCOPE),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTexturesRegistered = true;
                        m_lastAnalyzerPtr = (void*)analyzer;
                    }

                    float windowW = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    float imgSize = (windowW - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                    imgSize = std::min(imgSize, 512.0f);
                    if (imgSize < 100.0f) imgSize = 256.0f;

                    // Row 1: Histogram | Waveform
                    ImGui::Text("Histogram (Brightness Distribution)");
                    ImGui::Image(m_scopeTextureIDs[0], ImVec2(imgSize, imgSize));
                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::Text("Waveform (Luma vs Position)");
                    ImGui::Image(m_scopeTextureIDs[1], ImVec2(imgSize, imgSize));
                    ImGui::EndGroup();

                    // Row 2: Vectorscope | Info
                    ImGui::Text("Vectorscope (Chroma Distribution)");
                    ImGui::Image(m_scopeTextureIDs[2], ImVec2(imgSize, imgSize));
                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::Text("Reference");
                    ImGui::TextDisabled("Center = neutral gray");
                    ImGui::TextDisabled("Outward = saturation");
                    ImGui::TextDisabled("Clockwise = hue shift");
                    ImGui::EndGroup();
                }
            } else {
                ImGui::TextDisabled("Frame Analyzer not available.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Calibration applies live, Auto HDR toggles requires swapchain rebuild (Apply HDR Changes).");
    }
} // namespace vkBasalt
