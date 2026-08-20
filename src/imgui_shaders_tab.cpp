#include "imgui_overlay.hpp"
#include "overlay_manager.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "effect.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace vkBasalt {

    static std::string serializeChain(const std::vector<std::string>& chain) {
        std::string result;
        for (size_t j = 0; j < chain.size(); j++) {
            if (j) result += ":";
            result += chain[j];
        }
        return result;
    }

    static const char* kBuiltInEffects[] = {
        "fxaa", "cas", "deband", "smaa", "lut", "dls",
        "clarity", "clarityrcas", "crystalclear"
    };

    // Effect category priority for auto sort
    static int getEffectSortPriority(const std::string& name) {
        // 1. AA
        if (name == "smaa" || name == "fxaa") return 0;
        // 2. Debanding
        if (name == "deband") return 1;
        // 3. Color grading / LUT
        if (name == "lut" || name == "dls") return 2;
        // 4. Contrast / Clarity (CrystalClear includes CAS + grain, must come later.)
        if (name == "clarity" || name == "clarityrcas") return 3;
        if (name == "crystalclear") return 4;
        // 5. Standalone sharpening
        if (name == "cas") return 5;
        // 6. Unknown / ReShade effects
        return 6;
    }

    void ImGuiOverlay::resetParamToDefault(Effect* effect, const EffectParamDesc& p) {
        if (p.type == ParamType::Combo) {
            if (!p.comboOptions.empty()) {
                m_pConfig->setOption(p.key, p.comboOptions[0]);
            }
        } else if (p.type == ParamType::FilePath) {
            m_pConfig->setOption(p.key, "");
        } else if (p.type == ParamType::Bool || p.type == ParamType::Int) {
            effect->setParam(p.key, p.defaultVal);
            m_pConfig->setOption(p.key, std::to_string((int)p.defaultVal));
        } else {
            effect->setParam(p.key, p.defaultVal);
            std::string vs = std::to_string(p.defaultVal);
            std::replace(vs.begin(), vs.end(), ',', '.');
            m_pConfig->setOption(p.key, vs);
        }
    }

    void ImGuiOverlay::setParamDebounced(const std::string& key, const std::string& value) {
        m_pConfig->setOption(key, value);
        m_hasUnsavedChanges = true;
        m_previewDirty = true;
        m_lastChangeTime = ImGui::GetTime();
    }

    void ImGuiOverlay::setParamImmediate(const std::string& key, const std::string& value) {
        m_pConfig->setOption(key, value);
        m_hasUnsavedChanges = true;
        g_triggerPreviewReload = true;
    }

    void ImGuiOverlay::drawParamWidget(const EffectParamDesc* p, Effect* selectedEffect) {
        auto paramContextMenu = [&]() {
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Reset to Default")) {
                    resetParamToDefault(selectedEffect, *p);
                    m_hasUnsavedChanges = true;
                    g_triggerPreviewReload = true;
                }
                ImGui::EndPopup();
            }
        };

        switch (p->type) {
            case ParamType::Float: {
                float val = (float)selectedEffect->getParam(p->key);
                float step = (p->step > 0) ? (float)p->step : 0.01f;
                float range = (float)(p->maxVal - p->minVal);
                float dragSpeed = range / 200.0f;
                bool changed = false;
                ImGui::PushItemWidth(ImGui::CalcItemWidth());
                if (ImGui::DragFloat(p->label.c_str(), &val, dragSpeed, (float)p->minVal, (float)p->maxVal, "%.3f"))
                    changed = true;
                if (ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { val -= step; changed = true; }
                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { val += step; changed = true; }
                }
                ImGui::PopItemWidth();
                if (changed) {
                    val = std::clamp(val, (float)p->minVal, (float)p->maxVal);
                    selectedEffect->setParam(p->key, (double)val);
                    setParamDebounced(p->key, doubleToConfigString(val));
                }
                paramContextMenu();
                break;
            }
            case ParamType::Int: {
                int val = (int)selectedEffect->getParam(p->key);
                int step = (p->step > 0) ? (int)p->step : 1;
                float range = (float)(p->maxVal - p->minVal);
                float dragSpeed = std::max(0.05f, range / 200.0f);
                bool changed = false;
                ImGui::PushItemWidth(ImGui::CalcItemWidth());
                if (ImGui::DragInt(p->label.c_str(), &val, dragSpeed, (int)p->minVal, (int)p->maxVal))
                    changed = true;
                if (ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { val -= step; changed = true; }
                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { val += step; changed = true; }
                }
                ImGui::PopItemWidth();
                if (changed) {
                    val = std::clamp(val, (int)p->minVal, (int)p->maxVal);
                    selectedEffect->setParam(p->key, (double)val);
                    setParamDebounced(p->key, std::to_string(val));
                }
                paramContextMenu();
                break;
            }
            case ParamType::Bool: {
                bool val = selectedEffect->getParam(p->key) > 0.5;
                if (ImGui::Checkbox(p->label.c_str(), &val)) {
                    selectedEffect->setParam(p->key, val ? 1.0 : 0.0);
                    setParamImmediate(p->key, val ? "1" : "0");
                }
                paramContextMenu();
                break;
            }
            case ParamType::Combo: {
                std::string currentStr = m_pConfig->getOption<std::string>(p->key, "");
                int currentIdx = 0;
                for (size_t ci = 0; ci < p->comboOptions.size(); ci++) {
                    if (p->comboOptions[ci] == currentStr) { currentIdx = (int)ci; break; }
                }
                const char* preview = p->comboOptions.empty() ? "" : p->comboOptions[currentIdx].c_str();
                if (ImGui::BeginCombo(p->label.c_str(), preview)) {
                    for (size_t ci = 0; ci < p->comboOptions.size(); ci++) {
                        bool is_sel = (currentIdx == (int)ci);
                        if (ImGui::Selectable(p->comboOptions[ci].c_str(), is_sel)) {
                            selectedEffect->setParam(p->key, (double)ci);
                            setParamImmediate(p->key, p->comboOptions[ci]);
                        }
                        if (is_sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                paramContextMenu();
                break;
            }
            case ParamType::FilePath: {
                std::string currentPath = m_pConfig->getOption<std::string>(p->key, "");
                char pathBuf[1024] = {};
                strncpy(pathBuf, currentPath.c_str(), sizeof(pathBuf) - 1);
                ImGui::Text("%s", p->label.c_str());
                ImGui::PushItemWidth(-1.0f);
                if (ImGui::InputText("##filepath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    setParamDebounced(p->key, std::string(pathBuf));
                }
                ImGui::PopItemWidth();

                if (m_browserDir.empty()) {
                    const char* home = getenv("HOME");
                    m_browserDir = home ? std::string(home) : ".";
                }
                if (ImGui::Button("Browse...")) {
                    m_showBrowser = !m_showBrowser;
                }
                if (m_showBrowser) {
                    ImGui::BeginChild("##file_browser", ImVec2(0, 200), true);
                    ImGui::Text("Directory: %s", m_browserDir.c_str());

                    std::error_code ec;
                    if (std::filesystem::exists(m_browserDir, ec)) {
                        for (auto& entry : std::filesystem::directory_iterator(m_browserDir, ec)) {
                            std::string name = entry.path().filename().string();
                            if (entry.is_directory()) {
                                if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                                    m_browserDir = entry.path().string();
                                }
                            } else if (name.size() > 5 && name.substr(name.size() - 5) == ".cube") {
                                if (ImGui::Selectable(name.c_str())) {
                                    setParamImmediate(p->key, entry.path().string());
                                    m_showBrowser = false;
                                }
                            }
                        }
                    }
                    if (ImGui::Button(".. (parent)")) {
                        std::filesystem::path parent = std::filesystem::path(m_browserDir).parent_path();
                        if (!parent.empty()) m_browserDir = parent.string();
                    }
                    ImGui::EndChild();
                }
                break;
            }
        }
    }

    void ImGuiOverlay::drawShadersTab() {
        if (!m_pSwapchain) { ImGui::Text("No swapchain."); return; }
        drawChainPanel();
        ImGui::SameLine();
        drawEffectParamsPanel();
    }

    void ImGuiOverlay::drawChainPanel() {
        ImGui::BeginChild("##shader_list", ImVec2(260, 0), true);

        // Rebuild cache only when a reload trigger fired
        if (m_chainCacheDirty || g_triggerPreviewReload || g_triggerSoftReload || g_triggerRevertReload) {
            m_cachedChainList = m_pConfig->getOption<std::vector<std::string>>("effects", {"cas"});
            m_cachedAllEffects.clear();
            for (const char* b : kBuiltInEffects) m_cachedAllEffects.push_back(b);
            for (auto& name : m_cachedChainList) {
                bool isBuiltin = false;
                for (const char* b : kBuiltInEffects) { if (name == b) { isBuiltin = true; break; } }
                if (!isBuiltin) m_cachedAllEffects.push_back(name);
            }
            m_chainCacheDirty = false;
        }

        std::vector<std::string> chainList = m_cachedChainList;
        std::vector<std::string>& allEffects = m_cachedAllEffects;

        auto isInChain = [&](const std::string& name) {
            return std::find(chainList.begin(), chainList.end(), name) != chainList.end();
        };

        if (m_selectedEffectIndex >= allEffects.size()) m_selectedEffectIndex = 0;

        // Default to the first active chain effect
        if (!isInChain(allEffects[m_selectedEffectIndex]) && !chainList.empty()) {
            for (size_t i = 0; i < allEffects.size(); i++) {
                if (allEffects[i] == chainList[0]) {
                    m_selectedEffectIndex = i;
                    break;
                }
            }
        }

        // Master effects toggle indicator/button
        bool effectsEnabled = g_effectsEnabled.load();
        if (effectsEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.50f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.10f, 0.40f, 0.10f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.10f, 0.10f, 1.0f));
        }
        if (ImGui::Button(effectsEnabled ? "[ON] vkBasalt Effects" : "[OFF] vkBasalt Effects")) {
            g_effectsEnabled = !effectsEnabled;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to toggle all effects on/off (or press the toggle keybind)");
        }

        ImGui::Separator();
        ImGui::Text("Effect Chain");
        ImGui::TextDisabled("Checked = active, order top to bottom");
        ImGui::Separator();

        // Active effects iterate chainList directly so visual order = execution order
        for (size_t ci = 0; ci < chainList.size(); ci++) {
            // Map back to allEffects index for selection tracking
            size_t allIdx = 0;
            for (size_t i = 0; i < allEffects.size(); i++) {
                if (allEffects[i] == chainList[ci]) { allIdx = i; break; }
            }
            ImGui::PushID((int)allIdx);

            bool checked = true;
            if (ImGui::Checkbox("##en", &checked)) {
                chainList.erase(chainList.begin() + ci);
                setParamImmediate("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
                ImGui::PopID();
                break; // List mutated, stop iterating
            }
            ImGui::SameLine();

            float arrowBtnSize = ImGui::GetFrameHeight();
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float arrowsTotalWidth = arrowBtnSize * 2 + spacing * 3;
            float selectableWidth = ImGui::GetContentRegionAvail().x - arrowsTotalWidth;

            bool is_selected = (m_selectedEffectIndex == allIdx);
            if (ImGui::Selectable(chainList[ci].c_str(), is_selected, 0, ImVec2(selectableWidth, ImGui::GetFrameHeight())))
                m_selectedEffectIndex = allIdx;

            // Up arrow
            ImGui::SameLine(0, spacing);
            ImGui::BeginDisabled(ci == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                std::iter_swap(chainList.begin() + ci, chainList.begin() + ci - 1);
                setParamImmediate("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
            }
            ImGui::EndDisabled();

            // Down arrow
            ImGui::SameLine();
            ImGui::BeginDisabled(ci == chainList.size() - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                std::iter_swap(chainList.begin() + ci, chainList.begin() + ci + 1);
                setParamImmediate("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
            }
            ImGui::EndDisabled();

            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Inactive:");

        // Inactive effects (not in chain)
        for (size_t i = 0; i < allEffects.size(); i++) {
            if (isInChain(allEffects[i])) continue;
            ImGui::PushID((int)i);

            bool checked = false;
            if (ImGui::Checkbox("##en", &checked)) {
                // Add to chain
                chainList.push_back(allEffects[i]);
                setParamImmediate("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
            }
            ImGui::SameLine();

            bool is_selected = (m_selectedEffectIndex == i);
            ImVec4 col(0.5f, 0.5f, 0.5f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            float selWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable(allEffects[i].c_str(), is_selected, 0, ImVec2(selWidth, ImGui::GetFrameHeight())))
                m_selectedEffectIndex = i;
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::Separator();

        if (ImGui::Button("Auto-Sort (AA > Deband > Color > Contrast > Sharpen)")) {
            std::stable_sort(chainList.begin(), chainList.end(),
                            [](const std::string& a, const std::string& b) {
                                return getEffectSortPriority(a) < getEffectSortPriority(b);
                            });
            setParamImmediate("effects", serializeChain(chainList));
            m_chainCacheDirty = true;
        }

        ImGui::EndChild();
    }

    void ImGuiOverlay::drawEffectParamsPanel() {
        ImGui::BeginChild("##effect_params", ImVec2(0, 0), true);

        std::string selectedName = m_cachedAllEffects[m_selectedEffectIndex];
        bool inChain = std::find(m_cachedChainList.begin(), m_cachedChainList.end(), selectedName) != m_cachedChainList.end();

        ImGui::Text("Effect: %s", selectedName.c_str());
        if (!g_effectsEnabled) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[BYPASSED]");
        } else if (inChain) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[INACTIVE]");
        }

        // Find the live effect object
        Effect* selectedEffect = nullptr;
        for (auto& eff : m_pSwapchain->effects)
            if (eff->getName() == selectedName) { selectedEffect = eff.get(); break; }

        // Reset button right aligned in the row
        if (selectedEffect) {
            float resetWidth = ImGui::CalcTextSize("Reset to Default").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - resetWidth);
            if (ImGui::Button("Reset to Default")) {
                const auto& resetParams = selectedEffect->getParamDescs();
                for (const auto& p : resetParams) resetParamToDefault(selectedEffect, p);
                m_hasUnsavedChanges = true;
                g_triggerPreviewReload = true;
            }
        }

        ImGui::Separator();

        if (!selectedEffect) {
            ImGui::TextWrapped("Tick the checkbox to add this effect to the chain and edit its parameters.");
            ImGui::EndChild();
            return;
        }

        const auto& params = selectedEffect->getParamDescs();

        // Quality level gating (CrystalClear and future effects with quality tiers)
        int currentQuality = 4;
        bool hasQualityGating = false;
        for (const auto& p : params) {
            if (p.key.find("QualityLevel") != std::string::npos || p.key.find("qualityLevel") != std::string::npos) {
                hasQualityGating = true;
                currentQuality = (int)selectedEffect->getParam(p.key);
                break;
            }
        }

        // Search filter
        if (m_focusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "/ to search", m_searchFilter, sizeof(m_searchFilter));
        ImGui::Separator();
        if (params.empty()) {
            ImGui::TextWrapped("This effect has no configurable parameters.");
        } else {
            struct Category { const char* name; int sortOrder; std::vector<const EffectParamDesc*> items; };
            std::vector<Category> categories;

            auto getCategorySortOrder = [](const std::string& name) -> int {
                if (name == "Presets & Performance") return 0;
                if (name == "Sharpening & Contrast" || name == "Sharpening") return 1;
                if (name == "Anti-Aliasing (FXAA)" || name == "Anti-Aliasing") return 2;
                if (name == "Artifact Protection" || name == "Protection") return 3;
                if (name == "Film Grain & Dither" || name == "Film Grain") return 4;
                if (name == "Color & Tone") return 5;
                if (name == "Color Grading" || name == "Color Grade") return 6;
                if (name == "Blending") return 7;
                if (name == "Debug") return 99;
                return 50; // General / Unknown
            };

            auto findOrAddCategory = [&](const char* name) -> Category& {
                for (auto& c : categories) {
                    if (std::string(c.name) == name) return c;
                }
                categories.push_back({name, getCategorySortOrder(name), {}});
                return categories.back();
            };

            std::string searchLower(m_searchFilter);
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            bool hasSearch = !searchLower.empty();

            for (const auto& p : params) {
                if (hasSearch) {
                    std::string labelLower(p.label);
                    std::transform(labelLower.begin(), labelLower.end(), labelLower.begin(), ::tolower);
                    std::string keyLower(p.key);
                    std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                    if (labelLower.find(searchLower) == std::string::npos &&
                        keyLower.find(searchLower) == std::string::npos) continue;
                }

                const char* cat = "General";
                if (!p.category.empty()) {
                    cat = p.category.c_str();
                } else if (p.key.find("Preset") != std::string::npos) cat = "Presets & Performance";
                else if (p.key.find("Sharp") != std::string::npos || p.key.find("Cas") != std::string::npos || p.key.find("Bilateral") != std::string::npos || p.key.find("Contrast") != std::string::npos) cat = "Sharpening & Contrast";
                else if (p.key.find("AA") != std::string::npos || p.key.find("Fxaa") != std::string::npos || p.key.find("Smaa") != std::string::npos) cat = "Anti-Aliasing";
                else if (p.key.find("Edge") != std::string::npos || p.key.find("Guard") != std::string::npos || p.key.find("BandPass") != std::string::npos || p.key.find("Extreme") != std::string::npos || p.key.find("Shimmer") != std::string::npos || p.key.find("Clarity") != std::string::npos || p.key.find("Despeckle") != std::string::npos || p.key.find("Fringe") != std::string::npos || p.key.find("Checkerboard") != std::string::npos) cat = "Artifact Protection";
                else if (p.key.find("Grain") != std::string::npos || p.key.find("grain") != std::string::npos || p.key.find("Dither") != std::string::npos) cat = "Film Grain & Dither";
                else if (p.key.find("Vibrance") != std::string::npos || p.key.find("Deband") != std::string::npos || p.key.find("Tone") != std::string::npos || p.key.find("Specular") != std::string::npos || p.key.find("Saturation") != std::string::npos) cat = "Color & Tone";
                else if (p.key.find("CDL") != std::string::npos || p.key.find("ST") != std::string::npos || p.key.find("Temperature") != std::string::npos || p.key.find("Tint") != std::string::npos || p.key.find("Gamma") != std::string::npos || p.key.find("Lift") != std::string::npos || p.key.find("Clip") != std::string::npos) cat = "Color Grading";
                else if (p.key.find("Blend") != std::string::npos) cat = "Blending";
                else if (p.key.find("Debug") != std::string::npos) cat = "Debug";

                findOrAddCategory(cat).items.push_back(&p);
            }

            // Sort categories logically from top to bottom
            std::sort(categories.begin(), categories.end(), [](const Category& a, const Category& b) {
                return a.sortOrder < b.sortOrder;
            });

            bool focusedFirst = false;
            for (auto& cat : categories) {
                if (cat.items.empty()) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                bool open = ImGui::CollapsingHeader(cat.name, ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor();
                if (!open) continue;
                ImGui::Indent(8.0f);
                for (const auto* p : cat.items) {
                    ImGui::PushID(p->key.c_str());
                    if (m_justOpened && !focusedFirst) { ImGui::SetKeyboardFocusHere(); focusedFirst = true; }

                    bool paramDisabled = hasQualityGating && (currentQuality > selectedEffect->minQualityForParam(p->key));
                    if (paramDisabled) ImGui::BeginDisabled();
                    drawParamWidget(p, selectedEffect);
                    if (paramDisabled) ImGui::EndDisabled();

                    // Smart tooltip uses custom description if provided, otherwise auto generate range/default info
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        if (!p->tooltip.empty()) {
                            ImGui::SetTooltip("%s", p->tooltip.c_str());
                        } else {
                            std::string tip;
                            if (p->type == ParamType::Bool) {
                                tip = "Toggle\nDefault: " + std::string(p->defaultVal > 0.5 ? "On" : "Off");
                            } else if (p->type == ParamType::Combo) {
                                size_t defIdx = std::min((size_t)p->defaultVal, p->comboOptions.empty() ? 0 : p->comboOptions.size() - 1);
                                const char* def = p->comboOptions.empty() ? "None" : p->comboOptions[defIdx].c_str();
                                tip = "Options: " + std::to_string(p->comboOptions.size()) + "\nDefault: " + def;
                            } else if (p->type == ParamType::Int) {
                                tip = "Range: " + std::to_string((int)p->minVal) + " to " + std::to_string((int)p->maxVal) +
                                      "\nDefault: " + std::to_string((int)p->defaultVal);
                            } else { // Float
                                tip = "Range: " + doubleToConfigString(p->minVal) + " to " + doubleToConfigString(p->maxVal) +
                                      "\nDefault: " + doubleToConfigString(p->defaultVal);
                            }
                            ImGui::SetTooltip("%s", tip.c_str());
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::Unindent(8.0f);
            }
            m_justOpened = false;
        }

        // Live preview debounce (for parameter sliders, not checkbox toggles)
        if (m_previewDirty && m_hasUnsavedChanges) {
            float elapsed = ImGui::GetTime() - m_lastChangeTime;
            if (elapsed > 0.25f) {
                g_triggerPreviewReload = true;
                m_previewDirty = false;
            }
        }

        ImGui::EndChild();
    }

} // namespace vkBasalt
