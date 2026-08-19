#include "imgui_overlay.hpp"
#include "imgui_theme.hpp"
#include "config.hpp"
#include "imgui.h"

namespace vkBasalt {

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
            m_pConfig->setGlobalOption("themeBgAlpha", doubleToConfigString(bgAlpha));
            themeChanged = true;
        }
        if (ImGui::SliderFloat("Frame Rounding", &rounding, 0.0f, 12.0f, "%.1f")) {
            m_pConfig->setGlobalOption("themeRounding", doubleToConfigString(rounding));
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
