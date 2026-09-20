#include "hdr_detect.hpp"

#include <unistd.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <charconv>
#include <utility>

#include "config.hpp"
#include "logger.hpp"


namespace vkBasalt {

    static bool fileExists(const char* path) {
        struct stat st{};
        return stat(path, &st) == 0;
    }

    static bool readFileToString(const char* path, std::string& outStr) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) { fclose(f); return false; }
        outStr.resize(size);
        size_t read = fread(&outStr[0], 1, size, f);
        fclose(f);
        return std::cmp_equal(read ,size);
    }

    // Helper to extract a float value from a JSON block near a specific key
    static float extractJsonFloat(const std::string& block, const std::string& key) {
        size_t pos = block.find(key);
        if (pos == std::string::npos) return -1.0f;
        pos = block.find(':', pos);
        if (pos == std::string::npos) return -1.0f;
        
        float val = -1.0f;
        const char* start = block.c_str() + pos + 1;
        const char* end = block.c_str() + block.size();
        std::from_chars(start, end, val);
        return val;
    }

    // Helper to extract a string value from a JSON block near a specific key
    static std::string extractJsonString(const std::string& block, const std::string& key) {
        size_t pos = block.find(key);
        if (pos == std::string::npos) return "";
        pos = block.find('"', pos + key.length());
        if (pos == std::string::npos) return "";
        size_t end = block.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return block.substr(pos + 1, end - pos - 1);
    }

    static std::vector<DisplayHdrInfo> parseKdeOutputs() {
        std::vector<DisplayHdrInfo> displays;
        const char* home = std::getenv("HOME");
        if (!home) return displays;

        // Try kwinoutputconfig.json first (Plasma 6)
        std::string kwinOutputPath = std::string(home) + "/.config/kwinoutputconfig.json";
        if (fileExists(kwinOutputPath.c_str())) {
            std::string content;
            if (readFileToString(kwinOutputPath.c_str(), content)) {
                size_t pos = 0;
                // Robust JSON object extractor that tracks brace depth while respecting string boundaries
                while (pos < content.length()) {
                    size_t startBrace = content.find('{', pos);
                    if (startBrace == std::string::npos) break;
                    
                    int depth = 1;
                    size_t endBrace = startBrace + 1;
                    bool inString = false;
                    
                    // Find matching closing brace
                    while (endBrace < content.length() && depth > 0) {
                        char c = content[endBrace];
                        if (c == '"' && (endBrace == 0 || content[endBrace-1] != '\\')) {
                            inString = !inString;
                        } else if (!inString) {
                            if (c == '{') depth++;
                            else if (c == '}') depth--;
                        }
                        if (depth > 0) endBrace++;
                    }
                    
                    if (depth == 0) {
                        std::string block = content.substr(startBrace, endBrace - startBrace + 1);
                        
                        // Check if it's a leaf object (no nested objects) to avoid parsing parent wrappers
                        bool isLeaf = true;
                        bool inStr = false;
                        for (size_t i = 1; i < block.length() - 1; ++i) {
                            char c = block[i];
                            if (c == '"' && (i == 0 || block[i-1] != '\\')) inStr = !inStr;
                            else if (!inStr && c == '{') { isLeaf = false; break; }
                        }
                        
                        if (isLeaf) {
                            size_t hdrKey = block.find("\"highDynamicRange\"");
                            if (hdrKey != std::string::npos) {
                                size_t colon = block.find(':', hdrKey);
                                if (colon != std::string::npos) {
                                    size_t valStart = block.find_first_not_of(" \t\r\n", colon + 1);
                                    if (valStart != std::string::npos && block.compare(valStart, 4, "true") == 0) {
                                        DisplayHdrInfo info;
                                        info.name = extractJsonString(block, "\"name\"");
                                        info.peakBrightnessNits = extractJsonFloat(block, "\"maxPeakBrightnessOverride\"");
                                        info.sdrWhitePointNits = extractJsonFloat(block, "\"sdrBrightness\"");
                                        
                                        if (info.peakBrightnessNits <= 0) info.peakBrightnessNits = 1000.0f;
                                        if (info.sdrWhitePointNits <= 0) info.sdrWhitePointNits = 203.0f;
                                        
                                        info.detected = true;
                                        info.source = "kde";
                                        
                                        if (!info.name.empty()) {
                                            displays.push_back(info);
                                        }
                                    }
                                }
                            }
                        }
                        pos = endBrace + 1;
                    } else {
                        pos = startBrace + 1; // Malformed JSON, advance to prevent infinite loop
                    }
                }
            }
        }

        // Fallback to kwinrc (Plasma 5 or older configs)
        if (displays.empty()) {
            std::string kwinrcPath = std::string(home) + "/.config/kwinrc";
            if (fileExists(kwinrcPath.c_str())) {
                FILE* f = fopen(kwinrcPath.c_str(), "r");
                if (f) {
                    char line[512];
                    bool inHdrSection = false;
                    float peak = -1.0f, white = -1.0f;
                    while (fgets(line, sizeof(line), f)) {
                        if (strstr(line, "[Windows_HDR]")) {
                            inHdrSection = true;
                            continue;
                        }
                        if (inHdrSection && line[0] == '[') {
                            break;
                        }
                        if (inHdrSection) {
                            if (peak < 0 && strncmp(line, "MaxLuminance=", 13) == 0) {
                                std::from_chars(line + 13, line + sizeof(line), peak);
                            }
                            if (white < 0 && strncmp(line, "Reference=", 10) == 0) {
                                std::from_chars(line + 10, line + sizeof(line), white);
                            }
                        }
                    }
                    fclose(f);
                    
                    if (peak > 0 || white > 0) {
                        DisplayHdrInfo info;
                        info.name = "kwinrc_default";
                        info.peakBrightnessNits = peak > 0 ? peak : 1000.0f;
                        info.sdrWhitePointNits = white > 0 ? white : 203.0f;
                        info.detected = true;
                        info.source = "kde";
                        displays.push_back(info);
                    }
                }
            }
        }

        return displays;
    }

    std::vector<DisplayHdrInfo> getAllDetectedHdrDisplays() {
        static std::vector<DisplayHdrInfo> cachedDisplays;
        static bool parsed = false;
        if (!parsed) {
            cachedDisplays = parseKdeOutputs();
            parsed = true;
        }
        return cachedDisplays;
    }

    DisplayHdrInfo detectDisplayHdrCalibration(Config* pConfig, const std::string& monitorName) {
        std::vector<DisplayHdrInfo> displays = getAllDetectedHdrDisplays();

        // 1. If we have a platform detected monitor name, try to match it exactly first
        if (!monitorName.empty()) {
            for (const auto& d : displays) {
                if (d.name == monitorName) {
                    Logger::info("Using platform-detected HDR display: " + d.name);
                    return d;
                }
            }
        }

        // 2. Check if user explicitly selected a target display in the UI
        std::string targetName = pConfig ? pConfig->getOption<std::string>("targetHdrDisplay", "auto") : "auto";
        
        if (targetName != "auto" && targetName != "default") {
            for (const auto& d : displays) {
                if (d.name == targetName) {
                    Logger::info("Using user-selected HDR display: " + d.name);
                    return d;
                }
            }
        }
        
        // 3. Auto-select, return the first detected HDR display
        if (!displays.empty()) {
            Logger::info("Auto-selected HDR display: " + displays[0].name);
            return displays[0];
        }
        
        // 4. Fallback defaults
        DisplayHdrInfo info;
        info.source = "default";
        Logger::info("HDR calibration: using fallback defaults");
        return info;
    }

} // namespace vkBasalt
