#include "display_info.hpp"
#include "logger.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <charconv>

namespace vkBasalt {

    static bool fileExists(const char* path) {
        struct stat st;
        return stat(path, &st) == 0;
    }

    static bool readBinaryFile(const char* path, std::vector<uint8_t>& outData) {
        FILE* f = fopen(path, "rb");
        if (!f) {
            Logger::debug(std::string("readBinaryFile: Failed to open ") + path);
            return false;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            Logger::debug(std::string("readBinaryFile: Empty or invalid file ") + path);
            fclose(f);
            return false;
        }
        outData.resize(size);
        size_t read = fread(outData.data(), 1, size, f);
        fclose(f);
        if (read != (size_t)size) {
            Logger::debug(std::string("readBinaryFile: Short read on ") + path);
            return false;
        }
        return true;
    }

    static bool readTextLine(const char* path, char* outBuf, size_t bufSize) {
        FILE* f = fopen(path, "r");
        if (!f) return false;
        bool ok = (fgets(outBuf, bufSize, f) != nullptr);
        fclose(f);
        return ok;
    }

    std::string parseEdidMonitorName(const std::vector<uint8_t>& edid) {
        if (edid.size() < 128) return "";
        
        // Extract monitor name from descriptor blocks (offsets 54, 72, 90, 108)
        for (size_t i = 54; i < 126; i += 18) {
            if (edid[i] == 0x00 && edid[i + 1] == 0x00 &&
                edid[i + 2] == 0x00 && edid[i + 3] == 0xFC) {
                std::string name(reinterpret_cast<const char*>(&edid[i + 5]), 13);
                size_t end = name.find_last_not_of("\n\r ");
                if (end != std::string::npos) {
                    return name.substr(0, end + 1);
                }
            }
        }
        return "";
    }

    bool parseEdidHdrCapabilities(const std::vector<uint8_t>& edid, DisplayHdrCapabilities& outCaps) {
        if (edid.size() < 256) return false;

        uint8_t extensionCount = edid[126];
        for (uint8_t ext = 0; ext < extensionCount && (128 + (ext + 1) * 128) <= edid.size(); ext++) {
            size_t extOffset = 128 + ext * 128;
            if (edid[extOffset] != 0x02) continue; // CTA-861 extension block

            uint8_t dataBlockOffset = edid[extOffset + 2];
            if (dataBlockOffset < 4 || dataBlockOffset > 127) continue;

            size_t pos = extOffset + 4;
            size_t end = extOffset + dataBlockOffset;

            while (pos < end) {
                uint8_t tag = (edid[pos] >> 5) & 0x07;
                uint8_t length = edid[pos] & 0x1F;

                if (tag == 6 && length >= 3) {
                    uint8_t eotf = edid[pos + 1];
                    bool pq = (eotf & 0x04) != 0;
                    bool hlg = (eotf & 0x08) != 0;
                    if (pq || hlg) {
                        outCaps.hdrSupported = true;
                        outCaps.pqSupported = pq;
                        outCaps.hlgSupported = hlg;
                        if (length >= 3) outCaps.maxLuminance = 50.0f * std::pow(2.0f, edid[pos + 3] / 32.0f);
                        if (length >= 4) outCaps.maxFrameAvgLuminance = 50.0f * std::pow(2.0f, edid[pos + 4] / 32.0f);
                        if (length >= 5) {
                            float ratio = edid[pos + 5] / 255.0f;
                            outCaps.minLuminance = outCaps.maxLuminance * (ratio * ratio) / 100.0f;
                        }
                    }
                }
                pos += 1 + length;
            }
        }
        return outCaps.hdrSupported;
    }

    // Tier 1: kscreen-doctor (KDE Plasma specific & usually calibrated by user)
    static bool tryReadKscreenDoctor(DisplayHdrCapabilities& caps) {
        const char* kscreenPath = nullptr;
        struct stat st;
        if (stat("/usr/bin/kscreen-doctor", &st) == 0) {
            kscreenPath = "/usr/bin/kscreen-doctor";
        } else if (stat("/usr/local/bin/kscreen-doctor", &st) == 0) {
            kscreenPath = "/usr/local/bin/kscreen-doctor";
        } else {
            return false;
        }

        std::string cmd = std::string(kscreenPath) + " -o outputs 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return false;
        char buffer[512];
        // Track state per output block to prevent cross contamination between multiple monitors
        bool currentHdrEnabled = false;
        float currentPeak = 0.0f;
        float currentSdrWhite = 0.0f;
        bool foundAnyHdr = false;
        float bestPeak = 0.0f;
        float bestSdrWhite = 0.0f;

        auto finalizeOutput = [&]() {
            if (currentHdrEnabled) {
                foundAnyHdr = true;
                // Take the highest peak among detected HDR displays
                if (bestPeak == 0.0f || currentPeak > bestPeak) {
                    bestPeak = currentPeak;
                    bestSdrWhite = currentSdrWhite;
                }
            }
            currentHdrEnabled = false;
            currentPeak = 0.0f;
            currentSdrWhite = 0.0f;
        };

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            if (line.find("Output:") == 0) {
                finalizeOutput();
            } else if (line.find("HDR: enabled") != std::string::npos) {
                currentHdrEnabled = true;
            } else if (line.find("SDR brightness:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    float val = 0;
                    std::from_chars(line.c_str() + pos + 1, line.c_str() + line.size(), val);
                    currentSdrWhite = val;
                }
            } else if (line.find("overridden with:") != std::string::npos) {
                size_t pos = line.find("overridden with:");
                if (pos != std::string::npos) {
                    float val = 0;
                    std::from_chars(line.c_str() + pos + 16, line.c_str() + line.size(), val);
                    currentPeak = val;
                }
            } else if (line.find("Peak brightness:") != std::string::npos && line.find("unknown") == std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    float val = 0;
                    std::from_chars(line.c_str() + pos + 1, line.c_str() + line.size(), val);
                    if (val > 0) currentPeak = val;
                }
            }
        }
        finalizeOutput();
        pclose(pipe);

        if (foundAnyHdr) {
            caps.hdrSupported = true;
            caps.pqSupported = true;
            caps.maxLuminance = bestPeak > 0 ? bestPeak : 1000.0f;
            caps.maxFrameAvgLuminance = bestSdrWhite > 0 ? bestSdrWhite : 203.0f;
            caps.minLuminance = 0.0f;
            caps.source = "kscreen";
            Logger::info("Display HDR capabilities from kscreen-doctor: max=" +
                         std::to_string((int)caps.maxLuminance) + " nits, SDR white=" +
                         std::to_string((int)caps.maxFrameAvgLuminance) + " nits");
            return true;
        }
        return false;
    }

    // Tier 2: Sysfs EDID
    static bool tryReadSysfsEdid(DisplayHdrCapabilities& caps, const std::string& monitorName) {
        DIR* dir = opendir("/sys/class/drm");
        if (!dir) return false;

        struct EdidCandidate {
            std::string name;
            std::string monitorName;
            std::vector<uint8_t> edidData;
        };
        std::vector<EdidCandidate> candidates;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.find('-') == std::string::npos) continue;

            std::string basePath = std::string("/sys/class/drm/") + name;
            std::string statusPath = basePath + "/status";

            if (fileExists(statusPath.c_str())) {
                char buf[32] = {};
                if (readTextLine(statusPath.c_str(), buf, sizeof(buf))) {
                    // Strip trailing whitespace/newlines for robust comparison
                    size_t len = strlen(buf);
                    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' ')) {
                        buf[--len] = '\0';
                    }
                    if (strcmp(buf, "connected") != 0) continue;
                }
            }

            std::string edidPath = basePath + "/edid";
            if (!fileExists(edidPath.c_str())) continue;

            std::vector<uint8_t> edidData;
            if (!readBinaryFile(edidPath.c_str(), edidData) || edidData.size() < 128) continue;

            // Validate EDID header
            if (!(edidData[0] == 0x00 && edidData[1] == 0xFF &&
                  edidData[2] == 0xFF && edidData[3] == 0xFF &&
                  edidData[4] == 0xFF && edidData[5] == 0xFF &&
                  edidData[6] == 0xFF && edidData[7] == 0x00)) {
                continue;
            }

            EdidCandidate cand;
            cand.name = name;
            cand.monitorName = parseEdidMonitorName(edidData);
            cand.edidData = std::move(edidData);
            candidates.push_back(std::move(cand));
        }
        closedir(dir);

        if (candidates.empty()) return false;

        // Find the best candidate
        const EdidCandidate* best = nullptr;
        
        // 1. Match by requested monitor name (e.g. "DP-1" matches "card0-DP-1")
        if (!monitorName.empty()) {
            for (const auto& c : candidates) {
                if (c.name.find(monitorName) != std::string::npos) {
                    best = &c;
                    break;
                }
            }
        }

        // 2. Fallback: Prefer the first candidate with valid HDR metadata
        if (!best) {
            for (const auto& c : candidates) {
                DisplayHdrCapabilities tempCaps;
                if (parseEdidHdrCapabilities(c.edidData, tempCaps)) {
                    best = &c;
                    break;
                }
            }
        }

        // 3. Fallback: Just take the first connected monitor
        if (!best) best = &candidates[0];

        caps.monitorName = best->monitorName;
        parseEdidHdrCapabilities(best->edidData, caps);
        caps.source = "edid";

        Logger::info(std::string("Display HDR capabilities from EDID (") + best->name + "): " +
                     "max=" + std::to_string((int)caps.maxLuminance) + " nits, " +
                     "maxFALL=" + std::to_string((int)caps.maxFrameAvgLuminance) + " nits, " +
                     "min=" + std::to_string(caps.minLuminance) + " nits, " +
                     "HDR=" + (caps.hdrSupported ? "yes" : "no"));
        return true;
    }

    DisplayHdrCapabilities readDisplayHdrCapabilities(const std::string& monitorName) {
        DisplayHdrCapabilities caps;

        // Tier 1: kscreen-doctor
        if (tryReadKscreenDoctor(caps)) {
            return caps;
        }

        // Tier 2: Sysfs EDID
        if (tryReadSysfsEdid(caps, monitorName)) {
            return caps;
        }

        Logger::debug("No display HDR capabilities found");
        caps.source = "fallback";
        return caps;
    }

} // namespace vkBasalt
