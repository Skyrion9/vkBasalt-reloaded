#include "imgui_overlay.hpp"
#include "imgui_constants.hpp"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <math.h>

#include <string>
#include <algorithm>
#include <future>
#include <utility>

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

namespace vkBasalt
{

    void ImGuiOverlay::drawAutoHdrTab()
    {
        ImGui::Text("Auto HDR & Display Calibration");
        ImGui::Separator();
        ImGui::Spacing();

        bool gameIsHDR = false;
        if (m_pSwapchain) {
            ColorSpaceMode srcCsm = getColorSpaceMode(m_pSwapchain->sourceFormat, m_pSwapchain->sourceColorSpace);
            gameIsHDR             = (srcCsm != ColorSpaceMode::SDR_SRGB);
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
                ImGui::TextDisabled(
                    "Source:  %s / %s", formatName(m_pSwapchain->sourceFormat),
                    colorSpaceName(m_pSwapchain->sourceColorSpace));
                ImGui::TextDisabled(
                    "Output:  %s / %s", formatName(m_pSwapchain->destFormat),
                    colorSpaceName(m_pSwapchain->destColorSpace));

                // Support flags on one line
                ImGui::TextDisabled(
                    "Mutable: %s | hdr_metadata: %s | State: %s",
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
                capsFuture                = std::async(
                    std::launch::async, [uiMonitorName]() { return readDisplayHdrCapabilities(uiMonitorName); });
            }
            if (capsFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                displayCaps = capsFuture.get();
            } else {
                displayCaps.source = "loading";
            }

            bool hdrActive = m_pSwapchain && m_pSwapchain->autoHdrActive;

            // Adaptive analysis status
            if (analyzer && hdrActive) {
                float liveWhite = NAN, livePeak = NAN, liveIntensity = NAN;
                analyzer->getCurrentMetrics(liveWhite, livePeak, liveIntensity);

                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Adaptive Analysis: ACTIVE");
                ImGui::Text(
                    "  Scene: White %.1f nits | Peak %.1f nits | Intensity %.2f", liveWhite, livePeak, liveIntensity);

                ImGui::Spacing();
                ImGui::TextDisabled("Metadata Sent -> Display Reported:");

                // MaxCLL comparison
                bool peakOk = displayCaps.maxLuminance <= 0 || livePeak <= displayCaps.maxLuminance * 1.1f;
                ImGui::Text(
                    "  MaxCLL:  %.0f nits  ->  Display max: %.0f nits %s", livePeak, displayCaps.maxLuminance,
                    peakOk ? "" : "(EXCEEDS)");
                if (!peakOk) ImGui::SameLine();
                if (!peakOk) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "!");

                // MaxFALL comparison
                bool whiteOk =
                    displayCaps.maxFrameAvgLuminance <= 0 || liveWhite <= displayCaps.maxFrameAvgLuminance * 1.1f;
                ImGui::Text(
                    "  MaxFALL: %.0f nits  ->  Display FALL: %.0f nits %s", liveWhite, displayCaps.maxFrameAvgLuminance,
                    whiteOk ? "" : "(EXCEEDS)");
                if (!whiteOk) ImGui::SameLine();
                if (!whiteOk) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "!");

                // Static metadata on one line
                ImGui::TextDisabled("  Primaries: BT.2020 | White: D65 | Min: 0.0001 nits");

                ImGui::Spacing();

                // EDID status
                if (displayCaps.source != "fallback") {
                    ImGui::TextDisabled(
                        "EDID: %s%s%s | Max %.0f | FALL %.0f | Min %.4f nits", displayCaps.pqSupported ? "PQ" : "",
                        (displayCaps.pqSupported && displayCaps.hlgSupported) ? "+" : "",
                        displayCaps.hlgSupported ? "HLG" : "", displayCaps.maxLuminance,
                        displayCaps.maxFrameAvgLuminance, displayCaps.minLuminance);
                    if (!displayCaps.monitorName.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", displayCaps.monitorName.c_str());
                    }
                } else {
                    ImGui::TextDisabled("EDID: Could not read from /sys/class/drm");
                }

                if (ImGui::Button("Recheck EDID")) {
                    std::string uiMonitorName = m_pSwapchain ? m_pSwapchain->monitorName : "";
                    displayCaps               = readDisplayHdrCapabilities(uiMonitorName);
                    capsFuture                = std::async(
                        std::launch::async, [uiMonitorName]() { return readDisplayHdrCapabilities(uiMonitorName); });
                }

                // Visual verification as tooltip on a small text
                ImGui::TextDisabled("How to verify display is using metadata:");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "If your display supports local dimming, watch the dimming zones respond\n"
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
                    ImGui::TextDisabled(
                        "EDID: HDR %s | Max %.0f nits | %s%s%s", displayCaps.hdrSupported ? "YES" : "NO",
                        displayCaps.maxLuminance, displayCaps.pqSupported ? "PQ" : "",
                        (displayCaps.pqSupported && displayCaps.hlgSupported) ? "+" : "",
                        displayCaps.hlgSupported ? "HLG" : "");
                } else if (displayCaps.source == "loading") {
                    ImGui::TextDisabled("EDID: Loading display capabilities...");
                }
            }
        }

        ImGui::Spacing();

        // 3- Calibration
        bool isNativeHdr     = gameIsHDR;
        bool isAutoHdrActive = m_pSwapchain && m_pSwapchain->autoHdrActive;

        auto calibMode           = m_pConfig->getOption<std::string>("hdrCalibrationMode", "passthrough");
        bool isCalibrationActive = isAutoHdrActive || (isNativeHdr && calibMode != "off");
        bool showManualSliders   = isAutoHdrActive || (calibMode == "manual");

        if (ImGui::CollapsingHeader("Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool calibDisabled = (!isNativeHdr && !isAutoHdrActive);
            ImGui::BeginDisabled(calibDisabled);

            if (isAutoHdrActive) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Mode: Auto HDR (SDR -> HDR Expansion)");
            } else if (isNativeHdr) {
                int modeIdx             = (calibMode == "manual") ? 1 : (calibMode == "off" ? 2 : 0);
                const char* modeNames[] = {"Scene Analysis Only", "Manual Calibration", "Off"};
                ImGui::PushItemWidth(kSliderWidth);
                if (ImGui::BeginCombo("Native HDR Mode", modeNames[modeIdx])) {
                    for (int i = 0; i < 3; i++) {
                        bool is_sel = (modeIdx == i);
                        if (ImGui::Selectable(modeNames[i], is_sel)) {
                            std::string newMode = (i == 0) ? "passthrough" : (i == 1 ? "manual" : "off");
                            m_pConfig->setOption("hdrCalibrationMode", newMode);
                            m_pConfig->savePerGame();
                            invalidateScopeTextures();
                            g_triggerSoftReload = true;
                        }
                        if (is_sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                    ImGui::SetTooltip(
                        "Scene Analysis Only: Image passes through untouched, but scene luminance is measured\n"
                        "to send accurate dynamic MaxCLL/MaxFALL metadata to your display.\n\n"
                        "Manual Calibration: Apply custom SDR white point and peak brightness gain.\n"
                        "Use this if the game lacks its own HDR calibration slider.\n\n"
                        "Off: No processing or metadata updates.");
                }
            }
            ImGui::EndDisabled();

            if (!isCalibrationActive) {
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Calibration INACTIVE (effect not in chain)");
            } else if (!showManualSliders) {
                ImGui::TextDisabled("Image passthrough active. Manual sliders hidden.");
            }
            ImGui::Spacing();

            // General settings & Scene Analysis (enabled as long as calibration is active)
            ImGui::BeginDisabled(!isCalibrationActive);

            // Multi-Monitor Target Selection
            std::vector<DisplayHdrInfo> allDisplays = getAllDetectedHdrDisplays();
            if (allDisplays.size() > 1) {
                auto currentTarget      = m_pConfig->getOption<std::string>("targetHdrDisplay", "auto");
                std::string previewText = (currentTarget == "auto") ? "Auto-Detect (First HDR)" : currentTarget;

                ImGui::PushItemWidth(kSliderWidth);
                if (ImGui::BeginCombo("Target HDR Display", previewText.c_str())) {
                    if (ImGui::Selectable("Auto-Detect (First HDR)", currentTarget == "auto")) {
                        m_pConfig->setOption("targetHdrDisplay", "auto");
                        m_pConfig->savePerGame();
                        g_triggerSoftReload = true;
                    }
                    for (const auto& d : allDisplays) {
                        bool is_sel = (d.name == currentTarget);
                        std::string label =
                            d.name + " (" + std::to_string(static_cast<int>(d.peakBrightnessNits)) + " nits)";
                        if (ImGui::Selectable(label.c_str(), is_sel)) {
                            m_pConfig->setOption("targetHdrDisplay", d.name);
                            m_pConfig->savePerGame();
                            g_triggerSoftReload = true;
                        }
                        if (is_sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            // System detection + per-game toggle on one line
            std::string uiMonitorName = m_pSwapchain ? m_pSwapchain->monitorName : "";
            DisplayHdrInfo detected   = detectDisplayHdrCalibration(m_pConfig, uiMonitorName);
            std::string sourceStr     = (detected.source == "kde") ? "KDE Plasma" : "Defaults";
            ImGui::TextDisabled(
                "System: %s (Peak %.0f | White %.0f nits)", sourceStr.c_str(), detected.peakBrightnessNits,
                detected.sdrWhitePointNits);

            // Per-game calibration toggle
            const auto& calibParams = NitCalibrationEffect::getCalibrationParams();
            bool perGameCalib       = false;
            for (const auto& p : calibParams) {
                if (m_pConfig->hasPerGameOption(p.key)) {
                    perGameCalib = true;
                    break;
                }
            }
            if (ImGui::Checkbox("Per-game calibration", &perGameCalib)) {
                if (perGameCalib) {
                    for (const auto& p : calibParams) {
                        std::string val;
                        if (p.type == ParamType::Float) {
                            val = doubleToConfigString(
                                m_pConfig->getOption<float>(p.key, static_cast<float>(p.defaultVal)));
                        } else if (p.type == ParamType::Combo) {
                            val = m_pConfig->getOption<std::string>(
                                p.key, p.comboOptions.empty() ? "" : p.comboOptions[static_cast<int>(p.defaultVal)]);
                        } else if (p.type == ParamType::Bool) {
                            val = m_pConfig->getOption<bool>(p.key, p.defaultVal > 0.5) ? "true" : "false";
                        } else {
                            val = std::to_string(
                                static_cast<int>(m_pConfig->getOption<int>(p.key, static_cast<int>(p.defaultVal))));
                        }
                        m_pConfig->setOption(p.key, val);
                    }
                } else {
                    for (const auto& p : calibParams) {
                        m_pConfig->removePerGameOption(p.key);
                    }
                }
                m_pConfig->savePerGame();
                invalidateScopeTextures();
                g_triggerSoftReload = true;
            }

            ImGui::Spacing();

            // Adaptive Scene Analysis toggle
            {
                ImGui::PushID("hdrAdaptive");
                bool hdrAdaptive = m_pConfig->getOption<bool>("hdrAdaptive", true);
                if (ImGui::Checkbox("Adaptive Scene Analysis", &hdrAdaptive)) {
                    setConfigImmediate("hdrAdaptive", hdrAdaptive ? "true" : "false", perGameCalib);
                    invalidateScopeTextures();
                    g_triggerSoftReload = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                    ImGui::SetTooltip(
                        "Analyzes scene luminance per-frame to dynamically adjust HDR expansion.\n"
                        "Prevents highlight blowout in bright scenes and boosts midtones in dark scenes.\n"
                        "Adds ~0.05ms GPU compute overhead.");
                }
                ImGui::PopID();
            }

            bool hdrAdaptive = m_pConfig->getOption<bool>("hdrAdaptive", true);
            if (hdrAdaptive) {
                ImGui::Indent(kIndentWidth);
                drawAdaptiveSlider(
                    "hdrAdaptiveSpeed", "Adaptation Speed", "hdrAdaptiveSpeed", 2.0f, 0.01f, 2.0f, "%.2f",
                    "How quickly the adaptation responds to scene changes.\n"
                    "0.01 = very slow (~100s), 0.1 = slow (~10s), 0.5 = moderate (~2s), 2.0 = fast (~0.5s).\n"
                    "Lower values prevent visible 'brightness pumping' but respond slower to scene transitions.",
                    perGameCalib);
                drawAdaptiveSlider(
                    "hdrAdaptivePeakScale", "Peak Stability", "hdrAdaptivePeakScale", 0.0f, 0.0f, 1.0f, "%.2f",
                    "1.0 = fixed display peak (no 'eye adaptation').\n"
                    "0.0 = aggressive scene-based peak reduction (causes visible adaptation).\n"
                    "Intermediate values blend between the two.",
                    perGameCalib);
                drawAdaptiveSlider(
                    "hdrAdaptiveMidtoneRange", "Midtone Bias Range", "hdrAdaptiveMidtoneRange", 0.05f, 0.0f, 0.2f,
                    "%.3f",
                    "How much midtones are biased based on scene brightness.\n"
                    "0.0 = no bias (fixed white point).\n"
                    "0.05 = subtle +/-5%% bias (default).\n"
                    "0.2 = aggressive +/-20%% bias.",
                    perGameCalib);
                ImGui::Unindent(kIndentWidth);
            }

            ImGui::EndDisabled(); // Close general settings block
            ImGui::Spacing();

            // Manual only settings (Tone Mapper, SDR White Point, Peak Brightness), disabled in "Scene Analysis Only" mode.
            ImGui::BeginDisabled(!isCalibrationActive || !showManualSliders);

            // Render calibration params from the declarative params
            std::string uiMonitorNameParams = m_pSwapchain ? m_pSwapchain->monitorName : "";
            DisplayHdrInfo paramFallback    = detectDisplayHdrCalibration(m_pConfig, uiMonitorNameParams);

            for (const auto& p : calibParams) {
                ImGui::PushID(p.key.c_str());
                bool changed = false;
                switch (p.type) {
                    case ParamType::Combo: {
                        auto strVal    = m_pConfig->getOption<std::string>(p.key, "");
                        int currentIdx = 0;
                        for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                            if (p.comboOptions[ci] == strVal) {
                                currentIdx = static_cast<int>(ci);
                                break;
                            }
                        }
                        if (strVal.empty()) currentIdx = static_cast<int>(p.defaultVal);

                        ImGui::PushItemWidth(kSliderWidth);
                        if (ImGui::BeginCombo(p.label.c_str(), p.comboOptions[currentIdx].c_str())) {
                            for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                                bool is_sel = (std::cmp_equal(currentIdx, ci));
                                if (ImGui::Selectable(p.comboOptions[ci].c_str(), is_sel)) {
                                    currentIdx = static_cast<int>(ci);
                                    changed    = true;
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
                        auto fallback = static_cast<float>(p.defaultVal);
                        if (p.key == "sdrWhitePointNits" && paramFallback.detected
                            && paramFallback.sdrWhitePointNits > 0.0f)
                            fallback = paramFallback.sdrWhitePointNits;
                        if (p.key == "hdrPeakNits" && paramFallback.detected && paramFallback.peakBrightnessNits > 0.0f)
                            fallback = paramFallback.peakBrightnessNits;

                        auto val        = m_pConfig->getOption<float>(p.key, fallback);
                        float step      = (p.step > 0) ? static_cast<float>(p.step) : 1.0f;
                        auto range      = static_cast<float>(p.maxVal - p.minVal);
                        float dragSpeed = range / kDragSpeedDivisor;

                        ImGui::PushItemWidth(kSliderWidth);
                        if (ImGui::DragFloat(
                                p.label.c_str(), &val, dragSpeed, static_cast<float>(p.minVal),
                                static_cast<float>(p.maxVal), "%.0f")) {
                            changed = true;
                        }
                        if (ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
                            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                                val -= step;
                                changed = true;
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                                val += step;
                                changed = true;
                            }
                        }
                        ImGui::PopItemWidth();

                        if (changed) {
                            val = std::clamp(val, static_cast<float>(p.minVal), static_cast<float>(p.maxVal));
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

            ImGui::EndDisabled(); // Close isCalibrationActive || showManualSliders
        }

        ImGui::Spacing();

        // 5- HDR Metadata Debug Tool
        if (ImGui::CollapsingHeader("HDR Metadata Debug Tool", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Overrides the game image with a test pattern and forces custom HDR metadata. "
                "Use this to verify if your display's internal tone mapper and local dimming "
                "are actually reacting to the metadata sent by vkBasalt.");
            ImGui::Spacing();

            bool hdrPipelineActive =
                m_pSwapchain
                && (m_pSwapchain->autoHdrActive
                    || (gameIsHDR && m_pConfig->getOption<std::string>("hdrCalibrationMode", "passthrough") != "off"));

            if (!hdrPipelineActive) {
                ImGui::TextColored(
                    ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                    "Debug tool requires active HDR pipeline (Auto HDR or HDR Calibration).");
            }

            ImGui::BeginDisabled(!hdrPipelineActive);
            bool debugActive = g_hdrDebugToolActive.load();
            if (ImGui::Checkbox("Enable Debug Tool (Replaces Game Image)", &debugActive)) {
                g_hdrDebugToolActive.store(debugActive);
                g_triggerSoftReload = true;
            }
            ImGui::EndDisabled();

            if (debugActive) {
                ImGui::Indent(kIndentWidth);

                const char* patternNames[] = {
                    "10%% APL Window (Defeats ABL)", "Flat 100%% (Max Brightness)", "Flat 5%% (Dark)"};
                int pattern = HdrDebugEffect::s_patternType.load();
                if (ImGui::BeginCombo("Test Pattern", patternNames[pattern])) {
                    for (int i = 0; i < 3; i++) {
                        bool is_sel = (pattern == i);
                        if (ImGui::Selectable(patternNames[i], is_sel)) {
                            HdrDebugEffect::s_patternType.store(i);
                        }
                        if (is_sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::PushItemWidth(kSliderWidth);
                float peak = HdrDebugEffect::s_debugPeakNits.load();
                if (ImGui::SliderFloat("MaxCLL (Peak Nits)", &peak, 100.0f, 4000.0f, "%.0f nits"))
                    HdrDebugEffect::s_debugPeakNits.store(peak);

                float white = HdrDebugEffect::s_debugWhiteNits.load();
                if (ImGui::SliderFloat("MaxFALL (Avg Nits)", &white, 50.0f, 1000.0f, "%.0f nits"))
                    HdrDebugEffect::s_debugWhiteNits.store(white);

                float win = HdrDebugEffect::s_windowSize.load();
                if (ImGui::SliderFloat("Window Area", &win, 0.01f, 1.0f, "%.2f"))
                    HdrDebugEffect::s_windowSize.store(win);
                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::TextDisabled("Move the Peak Nits slider while watching the screen.");
                ImGui::TextDisabled("If the brightness/contrast of the pattern changes,");
                ImGui::TextDisabled("your display is reading and applying the metadata.");

                ImGui::Unindent(kIndentWidth);
            }
        }

        ImGui::Spacing();
        // 6 - Scopes
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
                    if (!m_scopeTexturesRegistered || reinterpret_cast<void*>(analyzer) != m_lastAnalyzerPtr) {
                        if (m_scopeTexturesRegistered) {
                            ImGui_ImplVulkan_RemoveTexture(
                                (VkDescriptorSet) static_cast<uintptr_t>(m_scopeTextureIDs[0]));
                            ImGui_ImplVulkan_RemoveTexture(
                                (VkDescriptorSet) static_cast<uintptr_t>(m_scopeTextureIDs[1]));
                            ImGui_ImplVulkan_RemoveTexture(
                                (VkDescriptorSet) static_cast<uintptr_t>(m_scopeTextureIDs[2]));
                        }
                        m_scopeTextureIDs[0] = (ImTextureID) ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(), analyzer->getScopeImageView(FrameAnalyzer::HISTOGRAM),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTextureIDs[1] = (ImTextureID) ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(), analyzer->getScopeImageView(FrameAnalyzer::WAVEFORM),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTextureIDs[2] = (ImTextureID) ImGui_ImplVulkan_AddTexture(
                            analyzer->getScopeSampler(), analyzer->getScopeImageView(FrameAnalyzer::VECTORSCOPE),
                            VK_IMAGE_LAYOUT_GENERAL);
                        m_scopeTexturesRegistered = true;
                        m_lastAnalyzerPtr         = reinterpret_cast<void*>(analyzer);
                    }

                    float windowW = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    float imgSize = (windowW - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                    imgSize       = std::min(imgSize, kScopeMaxSize);
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
        ImGui::TextDisabled(
            "Calibration applies live, Auto HDR toggles requires swapchain rebuild (Apply HDR Changes).");
    }
} // namespace vkBasalt
