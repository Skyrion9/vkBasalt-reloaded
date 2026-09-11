#pragma once
#include <string>
#include <vector>

namespace vkBasalt {

    struct DisplayHdrInfo {
        std::string name;
        std::string monitorName;
        float peakBrightnessNits = 1000.0f;
        float sdrWhitePointNits = 203.0f;
        bool detected = false;
        std::string source = "default";
    };

    class Config; // Forward declaration

    DisplayHdrInfo detectDisplayHdrCalibration(Config* pConfig = nullptr, const std::string& monitorName = "");

    // Returns a list of all detected HDR displays for the UI dropdown
    std::vector<DisplayHdrInfo> getAllDetectedHdrDisplays();

} // namespace vkBasalt
