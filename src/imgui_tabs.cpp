#include "imgui_overlay.hpp"
#include "imgui_theme.hpp"
#include "overlay_manager.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "screenshot.hpp"
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

    // Key name conversion for keybind UI
    static std::string imguiKeyToConfigName(ImGuiKey key) {
        switch (key) {
            case ImGuiKey_Tab:         return "Tab";
            case ImGuiKey_LeftArrow:   return "Left";
            case ImGuiKey_RightArrow:  return "Right";
            case ImGuiKey_UpArrow:     return "Up";
            case ImGuiKey_DownArrow:   return "Down";
            case ImGuiKey_PageUp:      return "Prior";
            case ImGuiKey_PageDown:    return "Next";
            case ImGuiKey_End:         return "End";
            case ImGuiKey_Home:        return "Home";
            case ImGuiKey_Insert:      return "Insert";
            case ImGuiKey_Delete:      return "Delete";
            case ImGuiKey_Backspace:   return "BackSpace";
            case ImGuiKey_Space:       return "space";
            case ImGuiKey_Enter:       return "Return";
            case ImGuiKey_Escape:      return "Escape";
            case ImGuiKey_F1: return "F1";   case ImGuiKey_F2: return "F2";
            case ImGuiKey_F3: return "F3";   case ImGuiKey_F4: return "F4";
            case ImGuiKey_F5: return "F5";   case ImGuiKey_F6: return "F6";
            case ImGuiKey_F7: return "F7";   case ImGuiKey_F8: return "F8";
            case ImGuiKey_F9: return "F9";   case ImGuiKey_F10: return "F10";
            case ImGuiKey_F11: return "F11"; case ImGuiKey_F12: return "F12";
            case ImGuiKey_0: return "0"; case ImGuiKey_1: return "1";
            case ImGuiKey_2: return "2"; case ImGuiKey_3: return "3";
            case ImGuiKey_4: return "4"; case ImGuiKey_5: return "5";
            case ImGuiKey_6: return "6"; case ImGuiKey_7: return "7";
            case ImGuiKey_8: return "8"; case ImGuiKey_9: return "9";
            case ImGuiKey_A: return "a"; case ImGuiKey_B: return "b";
            case ImGuiKey_C: return "c"; case ImGuiKey_D: return "d";
            case ImGuiKey_E: return "e"; case ImGuiKey_F: return "f";
            case ImGuiKey_G: return "g"; case ImGuiKey_H: return "h";
            case ImGuiKey_I: return "i"; case ImGuiKey_J: return "j";
            case ImGuiKey_K: return "k"; case ImGuiKey_L: return "l";
            case ImGuiKey_M: return "m"; case ImGuiKey_N: return "n";
            case ImGuiKey_O: return "o"; case ImGuiKey_P: return "p";
            case ImGuiKey_Q: return "q"; case ImGuiKey_R: return "r";
            case ImGuiKey_S: return "s"; case ImGuiKey_T: return "t";
            case ImGuiKey_U: return "u"; case ImGuiKey_V: return "v";
            case ImGuiKey_W: return "w"; case ImGuiKey_X: return "x";
            case ImGuiKey_Y: return "y"; case ImGuiKey_Z: return "z";
            default: return "";
        }
    }

    std::string ImGuiOverlay::doubleToConfigString(double val) {
        std::string s = std::to_string(val);
        std::replace(s.begin(), s.end(), ',', '.');
        return s;
    }

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

    void ImGuiOverlay::applyKeybind(int field, ImGuiKey key) {
        const char* configKeys[] = {"toggleKey", "reloadConfigKey", "overlayToggleKey", "screenshotKey"};
        std::string newName = imguiKeyToConfigName(key);
        if (newName.empty()) return;
        std::string myOldKey = m_pConfig->getOption<std::string>(configKeys[field], "");
        int otherField = -1;
        for (int i = 0; i < 4; i++) {
            if (i == field) continue;
            if (m_pConfig->getOption<std::string>(configKeys[i], "") == newName) {
                otherField = i;
                break;
            }
        }
        if (otherField >= 0) {
            if (otherField == 2 && myOldKey.empty()) return;
            m_pConfig->setOption(configKeys[otherField], myOldKey);
            m_pConfig->setOption(configKeys[field], newName);
        } else {
            m_pConfig->setOption(configKeys[field], newName);
        }
    }

    // Effect category priority for auto sort
    static int getEffectSortPriority(const std::string& name) {
        // 1. AA
        if (name == "smaa" || name == "fxaa") return 0;
        // 2.Debanding
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

    void ImGuiOverlay::drawShadersTab() {
        if (!m_pSwapchain) { ImGui::Text("No swapchain."); return; }
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
                m_pConfig->setOption("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
                m_hasUnsavedChanges = true;
                g_triggerPreviewReload = true;
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
                m_pConfig->setOption("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
                m_hasUnsavedChanges = true;
                g_triggerPreviewReload = true;
            }
            ImGui::EndDisabled();

            // Down arrow
            ImGui::SameLine();
            ImGui::BeginDisabled(ci == chainList.size() - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                std::iter_swap(chainList.begin() + ci, chainList.begin() + ci + 1);
                m_pConfig->setOption("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
                m_hasUnsavedChanges = true;
                g_triggerPreviewReload = true;
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
                m_pConfig->setOption("effects", serializeChain(chainList));
                m_chainCacheDirty = true;
                m_hasUnsavedChanges = true;
                g_triggerPreviewReload = true; // Immediate rebuild
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
            m_pConfig->setOption("effects", serializeChain(chainList));
            m_chainCacheDirty = true;
            m_hasUnsavedChanges = true;
            g_triggerPreviewReload = true;
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel parameters for selected effect
        ImGui::BeginChild("##effect_params", ImVec2(0, 0), true);
        std::string selectedName = allEffects[m_selectedEffectIndex];
        bool inChain = isInChain(selectedName);

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

        // Search filter
        if (m_focusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "/ to search", m_searchFilter, sizeof(m_searchFilter));
        ImGui::Separator();

        const auto& params = selectedEffect->getParamDescs();
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

                    // Right click context menu to reset individual params to default.
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
                                m_pConfig->setOption(p->key, doubleToConfigString(val));
                                m_hasUnsavedChanges = true;
                                m_previewDirty = true;
                                m_lastChangeTime = ImGui::GetTime();
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
                                m_pConfig->setOption(p->key, std::to_string(val));
                                m_hasUnsavedChanges = true;
                                m_previewDirty = true;
                                m_lastChangeTime = ImGui::GetTime();
                            }
                            paramContextMenu();
                            break;
                        }
                        case ParamType::Bool: {
                            bool val = selectedEffect->getParam(p->key) > 0.5;
                            if (ImGui::Checkbox(p->label.c_str(), &val)) {
                                selectedEffect->setParam(p->key, val ? 1.0 : 0.0);
                                m_pConfig->setOption(p->key, val ? "1" : "0");
                                m_hasUnsavedChanges = true;
                                g_triggerPreviewReload = true;
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
                                        m_pConfig->setOption(p->key, p->comboOptions[ci]);
                                        m_hasUnsavedChanges = true;
                                        g_triggerPreviewReload = true;
                                    }
                                    if (is_sel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            paramContextMenu();
                            break;
                        }
                        case ParamType::FilePath: {
                            // Show current path
                            std::string currentPath = m_pConfig->getOption<std::string>(p->key, "");
                            char pathBuf[1024] = {};
                            strncpy(pathBuf, currentPath.c_str(), sizeof(pathBuf) - 1);

                            ImGui::Text("%s", p->label.c_str());
                            ImGui::PushItemWidth(-1.0f);
                            if (ImGui::InputText("##filepath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                                m_pConfig->setOption(p->key, std::string(pathBuf));
                                m_hasUnsavedChanges = true;
                                m_previewDirty = true;
                                m_lastChangeTime = ImGui::GetTime();
                            }
                            ImGui::PopItemWidth();

                            // Simple file browser that lists .cube files from common directories
                            static bool showBrowser = false;
                            static std::string browserDir;
                            if (browserDir.empty()) {
                                const char* home = getenv("HOME");
                                browserDir = home ? std::string(home) : ".";
                            }

                            if (ImGui::Button("Browse...")) {
                                showBrowser = !showBrowser;
                            }

                            if (showBrowser) {
                                ImGui::BeginChild("##file_browser", ImVec2(0, 200), true);
                                ImGui::Text("Directory: %s", browserDir.c_str());

                                // List .cube files in current directory
                                std::error_code ec;
                                if (std::filesystem::exists(browserDir, ec)) {
                                    for (auto& entry : std::filesystem::directory_iterator(browserDir, ec)) {
                                        std::string name = entry.path().filename().string();
                                        if (entry.is_directory()) {
                                            if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                                                browserDir = entry.path().string();
                                            }
                                        } else if (name.size() > 5 && name.substr(name.size() - 5) == ".cube") {
                                            if (ImGui::Selectable(name.c_str())) {
                                                m_pConfig->setOption(p->key, entry.path().string());
                                                m_hasUnsavedChanges = true;
                                                g_triggerPreviewReload = true;
                                                showBrowser = false;
                                            }
                                        }
                                    }
                                }

                                // Navigate up
                                if (ImGui::Button(".. (parent)")) {
                                    std::filesystem::path parent = std::filesystem::path(browserDir).parent_path();
                                    if (!parent.empty()) browserDir = parent.string();
                                }
                                ImGui::EndChild();
                            }
                            break;
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

    void ImGuiOverlay::drawSettingsTab() {
        ImGui::Text("vkBasalt Settings");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Cursor Area Scale");
        ImGui::TextDisabled("Only change this if the mouse pointer is misbehaving.");
        ImGui::TextDisabled("0 = auto-detect from display. Controls coordinate mapping only.");
        float cursorScale = m_pConfig->getOption<float>("cursorScale", -1.0f);
        if (cursorScale < 0.0f) cursorScale = m_pConfig->getOption<float>("overlayScale", 0.0f);
        ImGui::PushItemWidth(200);
        if (ImGui::InputFloat("##cursorScale", &cursorScale, 0.05f, 0.25f, "%.2f")) {
            if (cursorScale < 0.0f) cursorScale = 0.0f;
            m_pConfig->setOption("cursorScale", doubleToConfigString(cursorScale));
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("UI Scale");
        ImGui::TextDisabled("Scales UI elements (padding, spacing, widgets). Does NOT affect mouse.");
        ImGui::TextDisabled("0 = auto-detect from display.");
        float uiScale = m_pConfig->getOption<float>("uiScale", 0.0f);
        ImGui::PushItemWidth(200);
        if (ImGui::InputFloat("##uiScale", &uiScale, 0.05f, 0.25f, "%.2f")) {
            if (uiScale < 0.0f) uiScale = 0.0f;
            m_pConfig->setOption("uiScale", doubleToConfigString(uiScale));
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("Font Scale");
        ImGui::TextDisabled("Additional multiplier for font size on top of UI Scale.");
        ImGui::TextDisabled("0 or 1 = font scales with UI Scale. Set >1 for larger text.");
        float fontScale = m_pConfig->getOption<float>("fontScale", 0.0f);
        ImGui::PushItemWidth(200);
        if (ImGui::InputFloat("##fontScale", &fontScale, 0.05f, 0.25f, "%.2f")) {
            if (fontScale < 0.0f) fontScale = 0.0f;
            m_pConfig->setOption("fontScale", doubleToConfigString(fontScale));
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("Keybinds");
        ImGui::TextDisabled("Click a binding, then press a key. Esc cancels.");
        ImGui::Spacing();

        const char* kbLabels[]  = {"Toggle Effects", "Reload Config", "Open Overlay", "Screenshot"};
        const char* kbConfigs[] = {"toggleKey", "reloadConfigKey", "overlayToggleKey", "screenshotKey"};

        for (int i = 0; i < 4; i++) {
            std::string current = m_pConfig->getOption<std::string>(kbConfigs[i], "");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", kbLabels[i]);
            ImGui::SameLine(200);

            if (m_bindingField == i) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.10f, 1.0f));
                ImGui::Button("Press any key...", ImVec2(160, 0));
                ImGui::PopStyleColor();
            } else {
                std::string label = (current.empty() ? "(none)" : current) + "##kb" + std::to_string(i);
                if (ImGui::Button(label.c_str(), ImVec2(160, 0))) {
                    m_bindingField = i;
                }
            }
        }

        if (m_bindingField >= 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_bindingField = -1;
            } else {
                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
                    ImGuiKey ik = (ImGuiKey)k;
                    // Skip mouse buttons, Escape, Enter, Space to prevent immediate rebind.
                    if (ik == ImGuiKey_Escape || ik == ImGuiKey_Enter || ik == ImGuiKey_Space) continue;
                    if (ik >= ImGuiKey_MouseLeft && ik <= ImGuiKey_MouseWheelY) continue;
                    if (ImGui::IsKeyPressed(ik)) {
                        applyKeybind(m_bindingField, ik);
                        m_bindingField = -1;
                        break;
                    }
                }
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Screenshot");
        ImGui::Spacing();

        bool saveBeforeAfter = m_pConfig->getOption<bool>("screenshotBeforeAfter", false);
        if (ImGui::Checkbox("Save before/after comparison", &saveBeforeAfter)) {
            m_pConfig->setOption("screenshotBeforeAfter", saveBeforeAfter ? "true" : "false");
            m_pConfig->savePerGame();
        }
        ImGui::TextDisabled("When enabled, screenshots include both the raw game output and the post-processed result.");

        ImGui::Spacing();

        // Format selector
        const char* ssFormats[] = {"png", "jpg", "bmp", "tga", "hdr"};
        std::string ssFmt = m_pConfig->getOption<std::string>("screenshotFormat", "png");
        int ssFormatIdx = 0;
        for (int i = 0; i < IM_ARRAYSIZE(ssFormats); i++) {
            if (ssFmt == ssFormats[i]) { ssFormatIdx = i; break; }
        }
        ImGui::PushItemWidth(120);
        if (ImGui::Combo("Format", &ssFormatIdx, ssFormats, IM_ARRAYSIZE(ssFormats))) {
            m_pConfig->setOption("screenshotFormat", ssFormats[ssFormatIdx]);
            m_pConfig->savePerGame();
        }
        ImGui::PopItemWidth();

        // JPEG quality slider (only relevant for jpg)
        if (ssFmt == "jpg" || ssFmt == "jpeg") {
            int ssQuality = m_pConfig->getOption<int>("screenshotQuality", 95);
            ImGui::PushItemWidth(200);
            if (ImGui::SliderInt("JPEG Quality", &ssQuality, 1, 100)) {
                m_pConfig->setOption("screenshotQuality", std::to_string(ssQuality));
                m_pConfig->savePerGame();
            }
            ImGui::PopItemWidth();
        }

        ImGui::Spacing();
        // Screenshot directory browser
        std::string currentDir = m_pConfig->getOption<std::string>("screenshotPath", "");
        ImGui::Text("Screenshot directory:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", currentDir.empty() ? "(default: ~/Pictures/vkBasalt-reloaded)" : currentDir.c_str());

        static bool showDirBrowser = false;
        static std::string browserDir;
        if (ImGui::Button(showDirBrowser ? "Close Browser" : "Browse...")) {
            showDirBrowser = !showDirBrowser;
            if (showDirBrowser) {
                browserDir = currentDir;
                if (browserDir.empty()) {
                    const char* home = getenv("HOME");
                    browserDir = home ? std::string(home) : ".";
                }
            }
        }
        ImGui::SameLine();
        if (!currentDir.empty() && ImGui::Button("Reset to Default")) {
            m_pConfig->setOption("screenshotPath", "");
            m_pConfig->savePerGame();
        }

        if (showDirBrowser) {
            ImGui::BeginChild("##dir_browser", ImVec2(0, 200), true);
            ImGui::Text("Browsing: %s", browserDir.c_str());
            ImGui::Separator();
            std::error_code ec;
            if (std::filesystem::exists(browserDir, ec)) {
                std::vector<std::string> dirs;
                for (auto& entry : std::filesystem::directory_iterator(browserDir, ec)) {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory() && !name.empty() && name[0] != '.') {
                        dirs.push_back(name);
                    }
                }
                std::sort(dirs.begin(), dirs.end());
                for (auto& name : dirs) {
                    if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                        browserDir = (std::filesystem::path(browserDir) / name).string();
                    }
                }
                if (dirs.empty()) ImGui::TextDisabled("(no subdirectories)");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "Directory does not exist.");
            }
            ImGui::Separator();
            if (ImGui::Button("Use This Directory")) {
                m_pConfig->setOption("screenshotPath", browserDir);
                m_pConfig->savePerGame();
                showDirBrowser = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(".. (parent)")) {
                std::filesystem::path parent = std::filesystem::path(browserDir).parent_path();
                if (!parent.empty()) browserDir = parent.string();
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (ImGui::Button("Take Screenshot")) {
            m_isOpen = false;
            g_triggerScreenshot = true;
            m_screenshotReopenCounter = 3;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(saved as %s)", ssFmt.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Behavior");
        ImGui::Spacing();
        bool enableOnLaunch = m_pConfig->getOption<bool>("enableOnLaunch", true);
        if (ImGui::Checkbox("Enable effects on launch", &enableOnLaunch)) {
            m_pConfig->setOption("enableOnLaunch", enableOnLaunch ? "true" : "false");
        }
        ImGui::BeginDisabled(true);
        bool depthCapture = m_pConfig->getOption<bool>("depthCapture", false);
        ImGui::Checkbox("Depth capture (not yet implemented)", &depthCapture);
        ImGui::EndDisabled();
        ImGui::Spacing();
    }

    void ImGuiOverlay::drawPresetsTab() {
        ImGui::Text("Preset Management");
        ImGui::Separator();
        ImGui::Spacing();

        static char presetName[256] = {};
        ImGui::PushItemWidth(300);
        ImGui::InputTextWithHint("##preset_name", "New preset name", presetName, sizeof(presetName));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Save Current as Preset")) {
            if (presetName[0] != '\0') {
                // Resolve selected effect by name from the unified allEffects list
                if (m_pSwapchain && m_selectedEffectIndex < m_cachedAllEffects.size()) {
                    std::string selectedName = m_cachedAllEffects[m_selectedEffectIndex];
                    for (auto& eff : m_pSwapchain->effects) {
                        if (eff->getName() == selectedName) {
                            const auto& params = eff->getParamDescs();
                            for (const auto& p : params) {
                                if (p.type == ParamType::Combo) continue;
                                double val = eff->getParam(p.key);
                                m_pConfig->setOption(p.key, doubleToConfigString(val));
                            }
                            break;
                        }
                    }
                }
                m_pConfig->savePreset(std::string(presetName));
                presetName[0] = '\0';
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto presets = m_pConfig->listPresets();
        if (presets.empty()) {
            ImGui::TextDisabled("No presets saved yet.");
            return;
        }

        ImGui::Text("Saved Presets (%d):", (int)presets.size());
        ImGui::Spacing();

        for (size_t i = 0; i < presets.size(); i++) {
            ImGui::PushID((int)i);
            ImGui::Text("%s", presets[i].c_str());
            ImGui::SameLine(300);
            if (ImGui::Button("Load")) {
                m_pConfig->loadPreset(presets[i]);
                m_pConfig->savePerGame();
                g_triggerSoftReload = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                m_pConfig->deletePreset(presets[i]);
            }
            ImGui::PopID();
        }
    }

    void ImGuiOverlay::drawStyleTab() {
        ImGui::Text("UI Theme Customization");
        ImGui::Separator();
        ImGui::Spacing();

        float bg[3], accent[3], text[3];
        hexToRgb(m_pConfig->getOption<std::string>("themeBg", "1a0d33"), bg);
        hexToRgb(m_pConfig->getOption<std::string>("themeAccent", "47bf59"), accent);
        hexToRgb(m_pConfig->getOption<std::string>("themeText", "d9f2de"), text);
        float bgAlpha  = m_pConfig->getOption<float>("themeBgAlpha", 0.88f);
        float rounding = m_pConfig->getOption<float>("themeRounding", 3.0f);

        bool themeChanged = false;

        if (ImGui::ColorEdit3("Background Color", bg)) {
            m_pConfig->setGlobalOption("themeBg", rgbToHex(bg[0], bg[1], bg[2]));
            themeChanged = true;
        }
        if (ImGui::ColorEdit3("Accent Color", accent)) {
            m_pConfig->setGlobalOption("themeAccent", rgbToHex(accent[0], accent[1], accent[2]));
            themeChanged = true;
        }
        if (ImGui::ColorEdit3("Text Color", text)) {
            m_pConfig->setGlobalOption("themeText", rgbToHex(text[0], text[1], text[2]));
            themeChanged = true;
        }
        ImGui::Spacing();
        if (ImGui::SliderFloat("Background Opacity", &bgAlpha, 0.30f, 1.0f, "%.2f")) {
            m_pConfig->setGlobalOption("themeBgAlpha", std::to_string(bgAlpha));
            themeChanged = true;
        }
        if (ImGui::SliderFloat("Frame Rounding", &rounding, 0.0f, 12.0f, "%.1f")) {
            m_pConfig->setGlobalOption("themeRounding", std::to_string(rounding));
            themeChanged = true;
        }

        if (themeChanged) {
            m_pConfig->saveGlobal();
            applyThemeFromConfig(m_pConfig);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Reset to Default Theme")) {
            m_pConfig->setGlobalOption("themeBg", "1a0d33");
            m_pConfig->setGlobalOption("themeAccent", "47bf59");
            m_pConfig->setGlobalOption("themeText", "d9f2de");
            m_pConfig->setGlobalOption("themeBgAlpha", "0.88");
            m_pConfig->setGlobalOption("themeRounding", "3.0");
            m_pConfig->saveGlobal();
            applyThemeFromConfig(m_pConfig);
        }
    }

} // namespace vkBasalt
