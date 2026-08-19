#include "imgui_overlay.hpp"
#include "overlay_manager.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "effect.hpp"
#include "imgui.h"
#include <string>
#include <vector>

namespace vkBasalt {

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

} // namespace vkBasalt
