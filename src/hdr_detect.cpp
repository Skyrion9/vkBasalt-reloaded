#include "hdr_detect.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "logger.hpp"

namespace vkBasalt {

namespace fs = std::filesystem;

    // KDE Plasma 6 stores HDR calibration in ~/.config/kscreen/*.json with per display HDR calibration. We'll do a simple string search since we don't want to add a JSON dependency.
    static bool tryReadKdeCalibration(DisplayHdrInfo& info) {
        const char* home = std::getenv("HOME");
        if (!home) return false;

        float peak = -1.0f;
        float white = -1.0f;

        // Priority 1: kwinoutputconfig.json - KDE Plasma 6 stores per output HDR calibration here.
        std::string kwinOutputPath = std::string(home) + "/.config/kwinoutputconfig.json";
        if (fs::exists(kwinOutputPath)) {
            std::ifstream file(kwinOutputPath);
            if (file.good()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

                // Find the output with "highDynamicRange": true (the active HDR display) and extract maxPeakBrightnessOverride and sdrBrightness from it. We look for the HDR enabled block specifically.
                size_t hdrPos = content.find("\"highDynamicRange\": true");
                if (hdrPos == std::string::npos) {
                    hdrPos = content.find("\"highDynamicRange\":true");
                }

                if (hdrPos != std::string::npos) {
                    // Search backwards and forwards from the HDR block for the values maxPeakBrightnessOverride
                    size_t pos = content.find("\"maxPeakBrightnessOverride\"", hdrPos > 500 ? hdrPos - 500 : 0);
                    if (pos != std::string::npos && pos < hdrPos + 1000) {
                        pos = content.find(':', pos);
                        if (pos != std::string::npos) {
                            peak = std::atof(content.c_str() + pos + 1);
                        }
                    }

                    pos = content.find("\"sdrBrightness\"", hdrPos > 500 ? hdrPos - 500 : 0);
                    if (pos != std::string::npos && pos < hdrPos + 1000) {
                        pos = content.find(':', pos);
                        if (pos != std::string::npos) {
                            white = std::atof(content.c_str() + pos + 1);
                        }
                    }
                }
            }
        }

        // Priority 2: kwinrc [Windows_HDR], fallback if kwinoutputconfig.json didn't have the values.
        if (peak < 0 || white < 0) {
            std::string kwinrcPath = std::string(home) + "/.config/kwinrc";
            if (fs::exists(kwinrcPath)) {
                std::ifstream file(kwinrcPath);
                if (file.good()) {
                    std::string line;
                    bool inHdrSection = false;
                    while (std::getline(file, line)) {
                        if (line.find("[Windows_HDR]") != std::string::npos) {
                            inHdrSection = true;
                            continue;
                        }
                        if (inHdrSection && line[0] == '[') {
                            break; // New section, stop
                        }
                        if (inHdrSection) {
                            if (peak < 0 && line.find("MaxLuminance=") == 0) {
                                peak = std::atof(line.c_str() + 13);
                            }
                            if (white < 0 && line.find("Reference=") == 0) {
                                white = std::atof(line.c_str() + 10);
                            }
                        }
                    }
                }
            }
        }

        if (peak > 0 || white > 0) {
            if (peak > 0) info.peakBrightnessNits = peak;
            if (white > 0) info.sdrWhitePointNits = white;
            info.detected = true;
            info.source = "kde";
            Logger::info("HDR calibration from KDE Plasma: peak=" + std::to_string((int)info.peakBrightnessNits) +
                        " nits, white=" + std::to_string((int)info.sdrWhitePointNits) + " nits");
            return true;
        }

        return false;
    }

    DisplayHdrInfo detectDisplayHdrCalibration() {
        static DisplayHdrInfo cachedInfo;
        static bool detected = false;
        
        if (detected) return cachedInfo;

        DisplayHdrInfo info;

        // Priority 1: KDE Plasma calibration (hopefully) user calibrated values, does everyone calibrate??
        if (tryReadKdeCalibration(info)) {
            cachedInfo = info;
            detected = true;
            return cachedInfo;
        }

        // Priority 2: Fallback defaults
        info.source = "default";
        Logger::info("HDR calibration: using fallback defaults (peak=" +
                    std::to_string(info.peakBrightnessNits) + ", white=" +
                    std::to_string(info.sdrWhitePointNits) + ")");
        cachedInfo = info;
        detected = true;
        return cachedInfo;
    }

} // namespace vkBasalt
