#pragma once

#include <string>

namespace vkBasalt {
    class Config;

    // Hex color utilities
    void hexToRgb(const std::string& hex, float* out);
    std::string rgbToHex(float r, float g, float b);

    // Apply theme from config to the current ImGui style.
    // Requires a valid ImGui context.
    void applyThemeFromConfig(Config* pConfig);
} // namespace vkBasalt
