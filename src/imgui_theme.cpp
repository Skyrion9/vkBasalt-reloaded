#include "imgui_theme.hpp"
#include "config.hpp"
#include "imgui.h"
#include <cstdio>
#include <algorithm>

namespace vkBasalt {

    void hexToRgb(const std::string& hex, float* out) {
        unsigned int r = 0, g = 0, b = 0;
        if (hex.size() >= 6) {
            sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
        }
        out[0] = r / 255.0f;
        out[1] = g / 255.0f;
        out[2] = b / 255.0f;
    }

    std::string rgbToHex(float r, float g, float b) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x%02x%02x",
                (int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f));
        return std::string(buf);
    }

    void applyThemeFromConfig(Config* pConfig) {
        ImGuiStyle& style = ImGui::GetStyle();

        float bg[3], accent[3], text[3];
        hexToRgb(pConfig->getOption<std::string>("themeBg", "1a0d33"), bg);
        hexToRgb(pConfig->getOption<std::string>("themeAccent", "47bf59"), accent);
        hexToRgb(pConfig->getOption<std::string>("themeText", "d9f2de"), text);
        float bgAlpha  = pConfig->getOption<float>("themeBgAlpha", 0.88f);
        float rounding = pConfig->getOption<float>("themeRounding", 3.0f);

        ImVec4 bgDark(bg[0], bg[1], bg[2], bgAlpha);
        ImVec4 bgMid(bg[0] * 1.4f, bg[1] * 1.6f, bg[2] * 1.3f, bgAlpha);
        ImVec4 border(bg[0] * 4.2f, bg[1] * 5.6f, bg[2] * 3.4f, 1.0f);
        ImVec4 accentDark(accent[0] * 0.57f, accent[1] * 0.67f, accent[2] * 0.63f, 1.0f);
        ImVec4 accentBright(accent[0] * 1.6f, accent[1] * 1.23f, accent[2] * 1.43f, 1.0f);

        style.Colors[ImGuiCol_WindowBg]             = bgDark;
        style.Colors[ImGuiCol_ChildBg]              = ImVec4(bg[0] * 1.2f, bg[1] * 1.4f, bg[2] * 1.1f, bgAlpha);
        style.Colors[ImGuiCol_PopupBg]              = ImVec4(bg[0] * 0.8f, bg[1] * 0.8f, bg[2] * 0.8f, 0.95f);
        style.Colors[ImGuiCol_TitleBg]              = ImVec4(bg[0] * 0.8f, bg[1] * 0.8f, bg[2] * 0.8f, bgAlpha);
        style.Colors[ImGuiCol_TitleBgActive]        = bgMid;
        style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(bg[0] * 0.8f, bg[1] * 0.8f, bg[2] * 0.8f, 0.75f);
        style.Colors[ImGuiCol_MenuBarBg]            = bgMid;
        style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(bg[0] * 0.8f, bg[1] * 0.8f, bg[2] * 0.8f, 0.60f);
        style.Colors[ImGuiCol_Border]               = border;
        style.Colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
        style.Colors[ImGuiCol_Separator]            = border;
        style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_SeparatorActive]      = accentBright;
        style.Colors[ImGuiCol_Text]                 = ImVec4(text[0], text[1], text[2], 1.0f);
        style.Colors[ImGuiCol_TextDisabled]         = ImVec4(1.0f, 0.62f, 0.22f, 0.70f);
        style.Colors[ImGuiCol_FrameBg]              = ImVec4(0.0f, 0.0f, 0.0f, 0.85f);
        style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.04f, 0.10f, 0.04f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.06f, 0.14f, 0.06f, 1.0f);
        style.Colors[ImGuiCol_Button]               = accentDark;
        style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_ButtonActive]         = accentBright;
        style.Colors[ImGuiCol_Header]               = ImVec4(accent[0] * 0.5f, accent[1] * 0.56f, accent[2] * 0.57f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_HeaderActive]         = accentBright;
        style.Colors[ImGuiCol_ScrollbarGrab]        = accentDark;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive]  = accentBright;
        style.Colors[ImGuiCol_CheckMark]            = accentBright;
        style.Colors[ImGuiCol_SliderGrab]           = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive]     = accentBright;
        style.Colors[ImGuiCol_ResizeGrip]           = accentDark;
        style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_ResizeGripActive]     = accentBright;
        style.Colors[ImGuiCol_Tab]                  = bgMid;
        style.Colors[ImGuiCol_TabHovered]           = ImVec4(accent[0], accent[1], accent[2], 1.0f);
        style.Colors[ImGuiCol_TabSelected]          = accentDark;
        style.Colors[ImGuiCol_TabDimmed]            = bgDark;
        style.Colors[ImGuiCol_TabDimmedSelected]    = accentDark;
        style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(accent[0], accent[1], accent[2], 0.30f);
        style.Colors[ImGuiCol_NavCursor]            = accentBright;

        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = rounding;
        style.GrabRounding = std::max(0.0f, rounding - 1.0f);
        style.TabRounding = rounding;
    }

} // namespace vkBasalt
