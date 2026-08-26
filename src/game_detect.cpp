#include "game_detect.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace vkBasalt {

    // Helpers
    static std::string sanitizeName(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (std::isalnum((unsigned char)c)) result += c;
            else if (c == ' ' || c == '-' || c == '_') result += '_';
        }
        std::string collapsed;
        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == '_' && i + 1 < result.size() && result[i + 1] == '_') continue;
            collapsed += result[i];
        }
        return collapsed;
    }

    static std::string getExePath() {
        char buf[4096] = {0};
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        return (len > 0) ? std::string(buf, (size_t)len) : "";
    }

    // Extract clean filename from a path. Handles both Unix (/) and Windows (\), strips .exe extension for Wine processes.
    static std::string getExeName(const std::string& fullPath) {
        std::string name = fullPath;
        size_t slash = fullPath.find_last_of("/\\");
        if (slash != std::string::npos) name = fullPath.substr(slash + 1);
        if (name.size() > 4) {
            std::string ext = name.substr(name.size() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".exe") name = name.substr(0, name.size() - 4);
        }
        return name.empty() ? "unknown" : name;
    }

    static bool isWineProcess(const std::string& exeName) {
        return exeName.find("wine") != std::string::npos;
    }

    // Validate and return Steam AppID from environment. Filters out empty, "0", and "default" (Lutris placeholder).
    static std::string getSteamAppId() {
        const char* steamGameId = std::getenv("SteamGameId");
        const char* steamAppId  = std::getenv("SteamAppId");
        std::string appId = steamGameId ? steamGameId : (steamAppId ? steamAppId : "");
        if (appId.empty() || appId == "0" || appId == "default") return "";
        return appId;
    }

    static std::string findSteamGameName(const std::string& appId) {
        const char* home = std::getenv("HOME");
        if (!home) return "";
        std::string homeStr(home);

        std::vector<std::string> steamRoots = {
            homeStr + "/.steam/steam",
            homeStr + "/.local/share/Steam",
            homeStr + "/.steam/root",
        };

        const char* libPaths = std::getenv("STEAM_LIBRARY_PATHS");
        if (libPaths) {
            std::stringstream ss(libPaths);
            std::string path;
            while (std::getline(ss, path, ':')) {
                if (!path.empty()) steamRoots.push_back(path);
            }
        }

        // Parse libraryfolders.vdf for additional library paths
        for (const auto& root : {homeStr + "/.steam/steam", homeStr + "/.local/share/Steam"}) {
            std::string vdfPath = root + "/steamapps/libraryfolders.vdf";
            std::ifstream vdf(vdfPath);
            if (!vdf.good()) continue;
            std::string line;
            while (std::getline(vdf, line)) {
                size_t pos = line.find("\"path\"");
                if (pos == std::string::npos) continue;
                size_t q1 = line.find('"', pos + 6);
                if (q1 == std::string::npos) continue;
                size_t q2 = line.find('"', q1 + 1);
                if (q2 == std::string::npos) continue;
                steamRoots.push_back(line.substr(q1 + 1, q2 - q1 - 1));
            }
        }

        // Search for appmanifest_<appid>.acf
        std::string manifestName = "appmanifest_" + appId + ".acf";
        for (const auto& root : steamRoots) {
            std::string manifestPath = root + "/steamapps/" + manifestName;
            std::ifstream f(manifestPath);
            if (!f.good()) continue;
            std::string line;
            while (std::getline(f, line)) {
                size_t pos = line.find("\"name\"");
                if (pos == std::string::npos) continue;
                size_t q1 = line.find('"', pos + 6);
                if (q1 == std::string::npos) continue;
                size_t q2 = line.find('"', q1 + 1);
                if (q2 == std::string::npos) continue;
                std::string sanitized = sanitizeName(line.substr(q1 + 1, q2 - q1 - 1));
                if (!sanitized.empty()) return sanitized;
            }
        }
        return "";
    }

    static std::string findLutrisGameName() {
        // 1. Direct env var (set by newer Lutris versions)
        const char* gameName = std::getenv("LUTRIS_GAME_NAME");
        if (gameName && std::strlen(gameName) > 0) return std::string(gameName);

        // 2. Look up by UUID in Lutris game config YAML
        const char* uuid = std::getenv("LUTRIS_GAME_UUID");
        if (uuid && std::strlen(uuid) > 0) {
            const char* home = std::getenv("HOME");
            if (!home) return "";
            std::string ymlPath = std::string(home) + "/.config/lutris/games/" + uuid + ".yml";
            std::ifstream f(ymlPath);
            if (!f.good()) return "";
            std::string line;
            while (std::getline(f, line)) {
                // Top level "name:" has no leading whitespace
                if (line.find("name:") == 0) {
                    std::string name = line.substr(5);
                    size_t start = name.find_first_not_of(" \t\r\n");
                    size_t end = name.find_last_not_of(" \t\r\n");
                    if (start != std::string::npos && end != std::string::npos) {
                        return name.substr(start, end - start + 1);
                    }
                }
            }
        }
        return "";
    }

    // Parse /proc/self/cmdline to find the actual game .exe path. Wine processes (wine64-preloader, wine-preloader) receive the game executable as a cmdline argument.
    static std::string findWineGameExe() {
        std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
        if (!cmdline.good()) return "";

        std::string content((std::istreambuf_iterator<char>(cmdline)),
                            std::istreambuf_iterator<char>());

        // cmdline is null separated
        std::vector<std::string> args;
        size_t start = 0;
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == '\0') {
                if (i > start) args.push_back(content.substr(start, i - start));
                start = i + 1;
            }
        }

        // Find the first argument ending in .exe (case insensitive)
        for (const auto& arg : args) {
            if (arg.size() > 4) {
                std::string ext = arg.substr(arg.size() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".exe") return arg;
            }
        }
        return "";
    }

    struct GameIdentity {
        std::string steamAppId;
        std::string steamName;
        std::string lutrisName;
        std::string exePath;
        std::string exeName;
        bool isSteam  = false;
        bool isLutris = false;
        bool isWine   = false;
    };

    static GameIdentity resolveGameIdentity() {
        GameIdentity id;

        id.steamAppId = getSteamAppId();
        id.isSteam = !id.steamAppId.empty();
        if (id.isSteam) {
            id.steamName = findSteamGameName(id.steamAppId);
        }

        id.lutrisName = findLutrisGameName();
        id.isLutris = !id.lutrisName.empty();

        id.exePath = getExePath();
        id.exeName = getExeName(id.exePath);
        id.isWine  = isWineProcess(id.exeName);

        if (id.isWine) {
            std::string wineGameExe = findWineGameExe();
            if (!wineGameExe.empty()) {
                id.exeName = getExeName(wineGameExe);
            }
        }

        return id;
    }

    static const GameIdentity& getGameIdentity() {
        static GameIdentity identity = resolveGameIdentity();
        return identity;
    }

    // MD5 (RFC 1321)
    std::string md5Hash(const std::string& msg) {
        uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;

        static const uint32_t K[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee, 0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be, 0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa, 0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed, 0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c, 0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05, 0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039, 0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1, 0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
        };
        static const uint32_t S[64] = {
            7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
            5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
            4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
            6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
        };

        size_t origLen = msg.size();
        size_t newLen = origLen + 1;
        while (newLen % 64 != 56) newLen++;

        std::vector<uint8_t> data(newLen + 8, 0);
        std::memcpy(data.data(), msg.data(), origLen);
        data[origLen] = 0x80;
        uint64_t bitLen = (uint64_t)origLen * 8;
        std::memcpy(data.data() + newLen, &bitLen, 8);

        for (size_t offset = 0; offset < data.size(); offset += 64) {
            uint32_t M[16];
            for (int i = 0; i < 16; i++)
                M[i] = (uint32_t)data[offset+i*4] | ((uint32_t)data[offset+i*4+1] << 8) |
                    ((uint32_t)data[offset+i*4+2] << 16) | ((uint32_t)data[offset+i*4+3] << 24);

            uint32_t A = h0, B = h1, C = h2, D = h3;
            for (int i = 0; i < 64; i++) {
                uint32_t F, g;
                if (i < 16)      { F = (B & C) | (~B & D); g = i; }
                else if (i < 32) { F = (D & B) | (~D & C); g = (5*i + 1) % 16; }
                else if (i < 48) { F = B ^ C ^ D;          g = (3*i + 5) % 16; }
                else             { F = C ^ (B | ~D);       g = (7*i) % 16; }
                F = F + A + K[i] + M[g];
                A = D; D = C; C = B;
                B = B + ((F << S[i]) | (F >> (32 - S[i])));
            }
            h0 += A; h1 += B; h2 += C; h3 += D;
        }

        uint8_t digest[16];
        uint32_t hs[4] = {h0, h1, h2, h3};
        for (int i = 0; i < 4; i++) {
            digest[i*4]   = hs[i] & 0xff;
            digest[i*4+1] = (hs[i] >> 8) & 0xff;
            digest[i*4+2] = (hs[i] >> 16) & 0xff;
            digest[i*4+3] = (hs[i] >> 24) & 0xff;
        }
        char hex[33];
        for (int i = 0; i < 16; i++) std::sprintf(hex + i*2, "%02x", digest[i]);
        hex[32] = 0;
        return std::string(hex);
    }

    // Binary content hashing (first 4MB + file size for performance)
    static std::string hashBinaryContent(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) return "";

        const size_t CHUNK = 4 * 1024 * 1024; // 4MB
        std::vector<char> buf(CHUNK);
        f.read(buf.data(), CHUNK);
        size_t bytesRead = (size_t)f.gcount();
        if (bytesRead == 0) return "";

        std::string content(buf.data(), bytesRead);

        // Append file size to distinguish files with identical first 4MB
        f.seekg(0, std::ios::end);
        size_t fileSize = (size_t)f.tellg();
        content += "|" + std::to_string(fileSize);

        return md5Hash(content);
    }

    // Persistent game registry (so we can judge if a game was moved, updated or is entirely different game.)
    struct GameEntry {
        std::string id;
        std::string exeName;
        std::string path;
        std::string binaryMd5;
    };

    static std::string registryPath() {
        const char* home = std::getenv("HOME");
        std::string root = home ? home : ".";
        return root + "/.config/vkBasalt-reloaded/games/registry.conf";
    }

    static std::vector<GameEntry> loadRegistry() {
        std::vector<GameEntry> entries;
        std::ifstream f(registryPath());
        if (!f.good()) return entries;

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            GameEntry e;
            if (std::getline(ss, e.id, '|') &&
                std::getline(ss, e.exeName, '|') &&
                std::getline(ss, e.path, '|') &&
                std::getline(ss, e.binaryMd5)) {
                entries.push_back(e);
            }
        }
        return entries;
    }

    static void saveRegistry(const std::vector<GameEntry>& entries) {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(registryPath()).parent_path(), ec);

        std::ofstream f(registryPath());
        if (!f.good()) return;

        f << "# vkBasalt-reloaded game registry\n";
        f << "# Format: id|exe_name|last_known_path|binary_md5\n";
        for (auto& e : entries) {
            f << e.id << "|" << e.exeName << "|" << e.path << "|" << e.binaryMd5 << "\n";
        }
    }

    // Public API
    std::string computeGameId() {
        static std::string cachedId;
        static bool computed = false;
        if (computed) return cachedId;
        computed = true;

        const GameIdentity& id = getGameIdentity();

        if (id.isSteam) {
            if (!id.steamName.empty()) {
                Logger::debug("Steam game detected: " + id.steamAppId + " (" + id.steamName + ")");
                cachedId = "steam_" + id.steamAppId + "_" + id.steamName;
            } else {
                Logger::debug("Steam game detected: " + id.steamAppId + " (name not found)");
                cachedId = "steam_" + id.steamAppId;
            }
            return cachedId;
        }

        if (id.isLutris) {
            Logger::debug("Lutris game detected: " + id.lutrisName);
            cachedId = "lutris_" + sanitizeName(id.lutrisName);
            return cachedId;
        }

        // Non-Steam / Non-Lutris: registry based identification
        std::string fullPath = id.exePath.empty() ? "unknown" : id.exePath;
        std::string cleanName = id.exeName;

        // Hash binary for update/move detection
        std::string binaryMd5 = hashBinaryContent(fullPath);

        auto entries = loadRegistry();
        bool registryDirty = false;

        // handles game updated in-place
        for (auto& e : entries) {
            if (e.path == fullPath) {
                if (!binaryMd5.empty() && e.binaryMd5 != binaryMd5) {
                    e.binaryMd5 = binaryMd5;
                    registryDirty = true;
                    Logger::debug("Game matched by path (binary updated): " + e.id);
                } else {
                    Logger::debug("Game matched by path: " + e.id);
                }
                if (registryDirty) saveRegistry(entries);
                cachedId = e.id;
                return cachedId;
            }
        }

        // handles game moved to new directory
        if (!binaryMd5.empty()) {
            for (auto& e : entries) {
                if (e.binaryMd5 == binaryMd5) {
                    std::string oldPath = e.path;
                    e.path = fullPath;
                    saveRegistry(entries);
                    Logger::debug("Game matched by binary (moved from " + oldPath + "): " + e.id);
                    cachedId = e.id;
                    return cachedId;
                }
            }
        }

        // generate ID for new game
        std::string newId = cleanName + "_" + md5Hash(fullPath).substr(0, 8);

        // Ensure uniqueness
        for (auto& e : entries) {
            if (e.id == newId) {
                newId += "_" + md5Hash(fullPath + "salt").substr(0, 4);
                break;
            }
        }

        GameEntry newEntry;
        newEntry.id = newId;
        newEntry.exeName = cleanName;
        newEntry.path = fullPath;
        newEntry.binaryMd5 = binaryMd5;
        entries.push_back(newEntry);
        saveRegistry(entries);

        Logger::debug("New game registered: " + newId + " (" + fullPath + ")");
        cachedId = newId;
        return cachedId;
    }

    std::string getGameDisplayName() {
        static std::string cachedName;
        static bool computed = false;
        if (computed) return cachedName;
        computed = true;

        const GameIdentity& id = getGameIdentity();

        if (id.isSteam && !id.steamName.empty()) {
            std::string readable = id.steamName;
            std::replace(readable.begin(), readable.end(), '_', ' ');
            cachedName = readable;
            return cachedName;
        }

        if (id.isLutris) {
            cachedName = id.lutrisName;
            return cachedName;
        }

        cachedName = id.exeName;
        return cachedName;
    }

} // namespace vkBasalt
