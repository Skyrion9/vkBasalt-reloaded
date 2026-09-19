#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace vkBasalt {

    struct DisplayHdrCapabilities {
        bool hdrSupported          = false;
        bool pqSupported           = false; // SMPTE ST 2084 (HDR10)
        bool hlgSupported          = false; // Hybrid Log Gamma
        float maxLuminance         = 0.0f;  // nits
        float maxFrameAvgLuminance = 0.0f;  // nits
        float minLuminance         = 0.0f;  // nits
        std::string monitorName;
        std::string source;                // "kscreen", "edid", "fallback"
    };

    // Parses raw EDID binary data. Returns true if valid HDR metadata was found.
    bool parseEdidHdrCapabilities(const std::vector<uint8_t>& edid, DisplayHdrCapabilities& outCaps);
    std::string parseEdidMonitorName(const std::vector<uint8_t>& edid);

    // Reads capabilities from the OS specific display API (kscreen-doctor, sysfs).
    DisplayHdrCapabilities readDisplayHdrCapabilities(const std::string& monitorName = "");

} // namespace vkBasalt
