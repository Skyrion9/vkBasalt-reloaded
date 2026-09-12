#include "imgui_overlay.hpp"
#include "imgui_constants.hpp"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <string>
#include <algorithm>
#include <future>

#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "format.hpp"
#include "overlay_manager.hpp"
#include "config.hpp"
#include "frame_analyzer.hpp"
#include "hdr_detect.hpp"
#include "effect_nit_calibration.hpp"
#include "auto_hdr_analyzer.hpp"
#include "display_info.hpp"
#include "effect_hdr_debug.hpp"


namespace vkBasalt {

    void ImGuiOverlay::drawAutoHdrTab() {
        ImGui::Text("Auto HDR & Display Calibration");
        ImGui::Separator();
        ImGui::Spacing();

        bool gameIsHDR = false;
        if (m_pSwapchain) {
            ColorSpaceMode srcCsm = getColorSpaceMode(m_pSwapchain->sourceFormat, m_pSwapchain->sourceColorSpace);
            gameIsHDR = (srcCsm != ColorSpaceMode::SDR_SRGB);
        }

        //  1 - Status + HDR toggle
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_pSwapchain) {
                // Compact status line with color indicator
                if (gameIsHDR) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Game: Native HDR");
                } else if (m_pSwapchain->autoHdrActive) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Game: SDR -> Auto HDR");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Game: SDR");
                }
                ImGui::SameLine();

                // Auto HDR toggle inline
                ImGui::BeginDisabled(gameIsHDR);
                bool autoHdr = m_pConfig->getOption<bool>("autoHdr", true);
                if (ImGui::Checkbox("Auto HDR", &autoHdr)) {
                    m_pConfig->setOption("autoHdr", autoHdr ? "on" : "off");
                    m_pConfig->savePerGame();
                    if (m_pSwapchain) m_pSwapchain->forceSwapchainRebuild = true;
                }
                ImGui::EndDisabled();
                if (gameIsHDR && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                    ImGui::SetTooltip("Disabled: Game is already outputting HDR.");
                }

                // Source -> Output format line
                ImGui::TextDisabled("Source:  %s / %s", formatName(m_pSwapchain->sourceFormat), colorSpaceName(m_pSwapchain->sourceColorSpace));
                ImGui::TextDisabled("Output:  %s / %s", formatName(m_pSwapchain->destFormat), colorSpaceName(m_pSwapchain->destColorSpace));

                // Support flags on one line
                ImGui::TextDisabled("Mutable: %s | hdr_metadata: %s | State: %s",
                    m_pSwapchain->pLogicalDevice->supportsMutableFormat ? "YES" : "NO",
                    m_pSwapchain->pLogicalDevice->supportsHdrMetadata ? "YES" : "NO",
                    m_pSwapchain->autoHdrActive ? "ACTIVE" : "idle");
            } else {
                ImGui::TextDisabled("No active swapchain.");
            }
        }

        ImGui::Spacing();

        // 2 - HDR Pipeline
        if (ImGui::CollapsingHeader("HDR Pipeline & Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Find analyzer once for this entire section
            AutoHdrAnalyzer* analyzer = nullptr;
            if (m_pSwapchain) {
                for (auto& effect : m_pSwapchain->effects) {
                    analyzer = effect->getAutoHdrAnalyzer();
                    if (analyzer) break;
                }
                if (!analyzer && m_pSwapchain->defaultHdrEffect) {
                    analyzer = m_pSwapchain->defaultHdrEffect->getAutoHdrAnalyzer();
                }
            }

            // Shared async state for EDID capabilities (accessible to both active and inactive UI blocks)
            static DisplayHdrCapabilities displayCaps;
            static std::shared_future<DisplayHdrCapabilities> capsFuture;
            if (!capsFuture.valid()) {
                std::string uiMonitorName = m_pSwapchain ? m_pSwapchain->monitorName : "";
                capsFuture = std::async(std::launch::async, [uiMonitorName]() {
                    return readDisplayHdrCapabilities(uiMonitorName);
                });
            }
            if (capsFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                displayCaps = capsFuture.get();
            } else {
                displayCaps.source = "loading";
            }

            bool hdrActive = m_pSwapchain && m_pSwapchain->autoHdrActive;

            // Adaptive analysis status
            if (analyzer && hdrActive) {
                float liveWhite, livePeak, liveIntensity;
                analyzer->getCurrentMetrics(liveWhite, livePeak, liveIntensity);

                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Adaptive Analysis: ACTIVE");
                ImGui::Text("  Scene: White %.1f nits | Peak %.1f nits | Intensity %.2f",
                            liveWhite, livePeak, liveIntensity);

                ImGui::Spacing();
                ImGui::TextDisabled("Metadata Sent -> Display Reported:");

                // MaxCLL comparison
                bool peakOk = displayCaps.maxLuminance <= 0 || livePeak <= displayCaps.maxLuminance * 1.1f;
                ImGui::Text("  MaxCLL:  %.0f nits  ->  Display max: %.0f nits %s",
                            livePeak, displayCaps.maxLuminance, peakOk ? "" : "(EXCEEDS)");
                if (!peakOk) ImGui::SameLine();
                if (!peakOk) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "!");

                // MaxFALL comparison
                bool whiteOk = displayCaps.maxFrameAvgLuminance <= 0 || liveWhite <= displayCaps.maxFrameAvgLuminance * 1.1f;
                ImGui::Text("  MaxFALL: %.0f nits  ->  Display FALL: %.0f nits %s",
                            liveWhite, displayCaps.maxFrameAvgLuminance, whiteOk ? "" : "(EXCEEDS)");
                if (!whiteOk) ImGui::SameLine();
                if (!whiteOk) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "!");

                // Static metadata on one line
                ImGui::TextDisabled("  Primaries: BT.2020 | White: D65 | Min: 0.0001 nits");

                ImGui::Spacing();

                // EDID status
                if (displayCaps.source != "fallback") {
                    ImGui::TextDisabled("EDID: %s%s%s | Max %.0f | FALL %.0f | Min %.4f nits",
                        displayCaps.pqSupported ? "PQ" : "",
                        (displayCaps.pqSupported && displayCaps.hlgSupported) ? "+" : "",
                        displayCaps.hlgSupported ? "HLG" : "",
                        displayCaps.maxLuminance, displayCaps.maxFrameAvgLuminance, displayCaps.minLuminance);
                    if (!displayCaps.monitorName.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", displayCaps.monitorName.c_str());
                    }
                } else {
                    ImGui::TextDisabled("EDID: Could not read from /sys/class/drm");
                }

            if (ImGui::Button("Recheck EDID")) {
                std::string uiMonitorName = m_pSwapchain ? m_pSwapchain->monitorName : "";
                displayCaps = readDisplayHdrCapabilities(uiMonitorName);
                capsFuture = std::async(std::launch::async, [uiMonitorName]() {
                    return readDisplayHdrCapabilities(uiMonitorName);
                });
            }

                // Visual verification as tooltip on a small text
                ImGui::TextDisabled("How to verify display is using metadata:");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("If your display supports local dimming, watch the dimming zones respond\n"
                                      "when bright highlights appear on screen. If they pulse or react to scene\n"
                                      "changes, the metadata is being consumed. If dimming is static regardless\n"
                                      "of content, the display firmware is ignoring the metadata and using its\n"
                                      "own internal scene analysis.");
                }
            } else if (hdrActive) {
                ImGui::TextDisabled("Analyzer not active (adaptive analysis may be disabled).");
            } else if (m_pSwapchain) {
                ImGui::TextDisabled("Auto HDR is not active. No metadata is being sent.");

                if (displayCaps.source != "fallback" && displayCaps.source != "loading") {
                    ImGui::TextDisabled("EDID: HDR %s | Max %.0f nits | %s%s%s",
                        displayCaps.hdrSupported ? "YES" : "NO",
                        displayCaps.maxLuminance,
                        displayCaps.pqSupported ? "PQ" : "",
                        (displayCaps.pqSupported && displayCaps.hlgSupported) ? "+" : "",
                        displayCaps.hlgSupported ? "HLG" : "");
                } else if (displayCaps.source == "loading") {
                    ImGui::TextDisabled("EDID: Loading display capabilities...");
                }
            }
        }

        ImGui::Spacing();

        bool isNativeHdr = gameIsHDR;
        bool isAutoHdrActive = m_pSwapchain && m_pSwapchain->autoHdrActive;
        bool hdrCalibConfig = m_pConfig->getOption<bool>("hdrCalibration", false);

        if (ImGui::CollapsingHeader("HDR Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("White point and peak brightness scaling for Auto HDR. Optionally usable for native HDR games.");
            ImGui::Spacing();
            bool displayChecked = isAutoHdrActive || hdrCalibConfig;
            bool isDisabled = isAutoHdrActive || (!isNativeHdr && !isAutoHdrActive);

            ImGui::BeginDisabled(isDisabled);
            if (ImGui::Checkbox("Enable HDR Calibration", &displayChecked)) {
                m_pConfig->setOption("hdrCalibration", displayChecked ? "on" : "off");
                m_pConfig->savePerGame();
                invalidateScopeTextures();
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
            const auto& calibParams = NitCalibrationEffect::getCalibrationParams();
            bool perGameCalib = false;
            for (const auto& p : calibParams) {
                if (m_pConfig->hasPerGameOption(p.key)) {
                    perGameCalib = true;
                    break;
                }
            }

        if (ImGui::Checkbox("Only change calibration for this game", &perGameCalib)) {
            if (perGameCalib) {
                for (const auto& p : calibParams) {
                    std::string val;
                    if (p.type == ParamType::Float) {
                        val = doubleToConfigString(m_pConfig->getOption<float>(p.key, (float)p.defaultVal));
                    } else if (p.type == ParamType::Combo) {
                        val = m_pConfig->getOption<std::string>(p.key, p.comboOptions.empty() ? "" : p.comboOptions[(int)p.defaultVal]);
                    } else if (p.type == ParamType::Bool) {
                        val = m_pConfig->getOption<bool>(p.key, p.defaultVal > 0.5) ? "true" : "false";
                    } else {
                        val = std::to_string((int)m_pConfig->getOption<int>(p.key, (int)p.defaultVal));
                    }
                    m_pConfig->setOption(p.key, val);
                }
                m_pConfig->savePerGame();
                invalidateScopeTextures();
                g_triggerSoftReload = true;
            } else {
                for (const auto& p : calibParams) {
                    m_pConfig->removePerGameOption(p.key);
                }
                m_pConfig->savePerGame();
                invalidateScopeTextures();
                g_triggerSoftReload = true;
            }
        }
            if (perGameCalib) {
                ImGui::TextDisabled("Calibration changes will be saved to this game's config only.");
            }

            ImGui::Spacing();

            DisplayHdrInfo detected = detectDisplayHdrCalibration(m_pConfig);
            std::string sourceStr = "Fallback Defaults";
            if (detected.source == "kde") sourceStr = "KDE Plasma";
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "System Detection: %s", sourceStr.c_str());
            ImGui::TextDisabled("Detected Peak: %.0f nits | White: %.0f nits",
                                detected.peakBrightnessNits, detected.sdrWhitePointNits);

            ImGui::Spacing();
            
            bool isCalibrationActive = isAutoHdrActive || (isNativeHdr && hdrCalibConfig);
            if (!isCalibrationActive) {
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Calibration is currently INACTIVE (Effect not in chain)");
            }
            ImGui::BeginDisabled(!isCalibrationActive);

            // Adaptive Scene Analysis toggle
            {
                ImGui::PushID("hdrAdaptive");
                bool hdrAdaptive = m_pConfig->getOption<bool>("hdrAdaptive", true);
                if (ImGui::Checkbox("Adaptive Scene Analysis", &hdrAdaptive)) {
                    setConfigImmediate("hdrAdaptive", hdrAdaptive ? "true" : "false", perGameCalib);
                    invalidateScopeTextures();
                    g_triggerSoftReload = true;
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default")) {
                        EffectParamDesc p; p.key = "hdrAdaptive"; p.type = ParamType::Bool; p.defaultVal = 1.0;
                        resetParamToConfig(p, perGameCalib);
                        invalidateScopeTextures();
                        g_triggerSoftReload = true;
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                    ImGui::SetTooltip("Analyzes scene luminance per-frame to dynamically adjust HDR expansion.\n"
                                    "Prevents highlight blowout in bright scenes and boosts midtones in dark scenes.\n"
                                    "Adds ~0.05ms GPU compute overhead.");
                }
                ImGui::PopID();
            }

            bool hdrAdaptive = m_pConfig->getOption<bool>("hdrAdaptive", true);
            if (hdrAdaptive) {
                ImGui::Indent(kIndentWidth);
                drawAdaptiveSlider("hdrAdaptiveSpeed", "Adaptation Speed", "hdrAdaptiveSpeed", 0.1f, 0.01f, 2.0f, "%.2f",
                    "How quickly the adaptation responds to scene changes.\n"
                    "0.01 = very slow (~100s), 0.1 = slow (~10s), 0.5 = moderate (~2s), 2.0 = fast (~0.5s).\n"
                    "Lower values prevent visible 'brightness pumping' but respond slower to scene transitions.", perGameCalib);

                drawAdaptiveSlider("hdrAdaptivePeakScale", "Peak Stability", "hdrAdaptivePeakScale", 1.0f, 0.0f, 1.0f, "%.2f",
                    "1.0 = fixed display peak (no pumping, recommended).\n"
                    "0.0 = aggressive scene-based peak reduction (original behavior, causes visible adaptation).\n"
                    "Intermediate values blend between the two.", perGameCalib);

                drawAdaptiveSlider("hdrAdaptiveMidtoneRange", "Midtone Bias Range", "hdrAdaptiveMidtoneRange", 0.05f, 0.0f, 0.2f, "%.3f",
                    "How much midtones are biased based on scene brightness.\n"
                    "0.0 = no bias (fixed white point).\n"
                    "0.05 = subtle +/-5%% bias (default).\n"
                    "0.2 = aggressive +/-20%% bias.", perGameCalib);
                ImGui::Unindent(kIndentWidth);
            }

            ImGui::Spacing();

            // Render calibration params from the declarative params
            for (const auto& p : calibParams) {
                ImGui::PushID(p.key.c_str());
                bool changed = false;

                switch (p.type) {
                    case ParamType::Combo: {
                        // Read current config value and map to combo index
                        std::string strVal = m_pConfig->getOption<std::string>(p.key, "");
                        int currentIdx = 0;
                        for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                            if (p.comboOptions[ci] == strVal) { currentIdx = (int)ci; break; }
                        }
                        if (strVal.empty()) currentIdx = (int)p.defaultVal;

                        ImGui::PushItemWidth(kSliderWidth);
                        if (ImGui::BeginCombo(p.label.c_str(), p.comboOptions[currentIdx].c_str())) {
                            for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                                bool is_sel = (currentIdx == (int)ci);
                                if (ImGui::Selectable(p.comboOptions[ci].c_str(), is_sel)) {
                                    currentIdx = (int)ci;
                                    changed = true;
                                }
                                if (is_sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();

                        if (changed) {
                            std::string modeStr = p.comboOptions[currentIdx];
                            setConfigImmediate(p.key, modeStr, perGameCalib);
                            invalidateScopeTextures();
                            g_triggerSoftReload = true;
                        }
                        break;
                    }
                    case ParamType::Float: {
                        float val = m_pConfig->getOption<float>(p.key, (float)p.defaultVal);
                        float step = (p.step > 0) ? (float)p.step : 1.0f;
                        float range = (float)(p.maxVal - p.minVal);
                        float dragSpeed = range / kDragSpeedDivisor;

                        ImGui::PushItemWidth(kSliderWidth);
                        if (ImGui::DragFloat(p.label.c_str(), &val, dragSpeed, (float)p.minVal, (float)p.maxVal, "%.0f")) {
                            changed = true;
                        }
                        if (ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
                            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { val -= step; changed = true; }
                            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { val += step; changed = true; }
                        }
                        ImGui::PopItemWidth();

                        if (changed) {
                            val = std::clamp(val, (float)p.minVal, (float)p.maxVal);
                            setConfigDebounced(p.key, doubleToConfigString(val), perGameCalib);
                        }
                        break;
                    }
                    default: break;
                }

                // Right click context menu for reset
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default")) {
                        resetParamToConfig(p, perGameCalib);
                        invalidateScopeTextures();
                        g_triggerSoftReload = true;
                    }
                    ImGui::EndPopup();
                }

                // Tooltip
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && !p.tooltip.empty()) {
                    ImGui::SetTooltip("%s", p.tooltip.c_str());
                }
                ImGui::Spacing();
                ImGui::PopID();
            }
            ImGui::EndDisabled(); // Close the isCalibrationActive block
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Scopes", ImGuiTreeNodeFlags_DefaultOpen)) {
            FrameAnalyzer* analyzer = nullptr;
            if (m_pSwapchain) {
                for (auto& pass : m_pSwapchain->computePasses) {
                    if (pass->asFrameAnalyzer()) {
                        analyzer = pass->asFrameAnalyzer();
                        break;
                    }
                }
            }

            if (analyzer) {
                bool enabled = analyzer->isEnabled();
                if (ImGui::Checkbox("Enable Scopes (GPU)", &enabled)) {
                    analyzer->setEnabled(enabled);
                    m_pConfig->setGlobalOption("scopesEnabled", enabled ? "true" : "false");
                    m_pConfig->saveGlobal();
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
                    imgSize = std::min(imgSize, kScopeMaxSize);
                    if (imgSize < kScopeMinSize) imgSize = kScopeFallbackSize;

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
