#pragma once
#include <string>
#include <cstdint>

namespace vkBasalt {

    struct DisplayHdrCapabilities {
        bool hdrSupported          = false;
        bool pqSupported           = false; // SMPTE ST 2084 (HDR10)
        bool hlgSupported          = false; // Hybrid Log Gamma
        float maxLuminance         = 0.0f;  // nits
        float maxFrameAvgLuminance = 0.0f;  // nits
        float minLuminance         = 0.0f;  // nits
        std::string monitorName;
        std::string source;                // "edid", "fallback"
    };

    DisplayHdrCapabilities readDisplayHdrCapabilities(const std::string& monitorName = "");

} // namespace vkBasalt
