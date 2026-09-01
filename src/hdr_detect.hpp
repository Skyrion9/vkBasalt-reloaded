#pragma once
#include <string>

namespace vkBasalt {

struct DisplayHdrInfo {
    float sdrWhitePointNits = 203.0f;
    float peakBrightnessNits = 1000.0f;
    bool detected = false;
    std::string source; // "kde" or "default"
};

// Attempts to detect display HDR calibration from the system (KDE Plasma config -> fallback defaults.)
DisplayHdrInfo detectDisplayHdrCalibration();

} // namespace vkBasalt
