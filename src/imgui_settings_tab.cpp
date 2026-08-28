#include "imgui_overlay.hpp"
#include "overlay_manager.hpp"
#include "screenshot.hpp"
#include "config.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdlib>
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
        const char* ssFormats[] = {"png", "jpg", "bmp", "tga", "hdr", "exr"};
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

        if (ImGui::Button(m_showDirBrowser ? "Close Browser" : "Browse...")) {
            m_showDirBrowser = !m_showDirBrowser;
            if (m_showDirBrowser) {
                m_dirBrowserDir = currentDir;
                if (m_dirBrowserDir.empty()) {
                    const char* home = getenv("HOME");
                    m_dirBrowserDir = home ? std::string(home) : ".";
                }
            }
        }
        ImGui::SameLine();
        if (!currentDir.empty() && ImGui::Button("Reset to Default")) {
            m_pConfig->setOption("screenshotPath", "");
            m_pConfig->savePerGame();
        }

        if (m_showDirBrowser) {
            ImGui::BeginChild("##dir_browser", ImVec2(0, 200), true);
            ImGui::Text("Browsing: %s", m_dirBrowserDir.c_str());
            ImGui::Separator();
            std::error_code ec;
            if (std::filesystem::exists(m_dirBrowserDir, ec)) {
                std::vector<std::string> dirs;
                for (auto& entry : std::filesystem::directory_iterator(m_dirBrowserDir, ec)) {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory() && !name.empty() && name[0] != '.') {
                        dirs.push_back(name);
                    }
                }
                std::sort(dirs.begin(), dirs.end());
                for (auto& name : dirs) {
                    if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                        m_dirBrowserDir = (std::filesystem::path(m_dirBrowserDir) / name).string();
                    }
                }
                if (dirs.empty()) ImGui::TextDisabled("(no subdirectories)");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "Directory does not exist.");
            }
            ImGui::Separator();
            if (ImGui::Button("Use This Directory")) {
                m_pConfig->setOption("screenshotPath", m_dirBrowserDir);
                m_pConfig->savePerGame();
                m_showDirBrowser = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(".. (parent)")) {
                std::filesystem::path parent = std::filesystem::path(m_dirBrowserDir).parent_path();
                if (!parent.empty()) m_dirBrowserDir = parent.string();
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

} // namespace vkBasalt
