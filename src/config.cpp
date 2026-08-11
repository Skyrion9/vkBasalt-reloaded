#include "config.hpp"
#include "logger.hpp"

#include <sstream>
#include <locale>
#include <filesystem>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <algorithm>
#include "game_detect.hpp"

namespace vkBasalt
{
    // Helpers
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string baseDir() {
        const char* home = std::getenv("HOME");
        std::string root = home ? home : ".";
        return root + "/.config/vkBasalt-reloaded";
    }

    // Construction / paths
    Config::Config() {
        ensureDirectories();

        m_globalPath = baseDir() + "/vkBasalt-reloaded.conf";

        std::string gameId = vkBasalt::computeGameId();
        m_gamePath = baseDir() + "/games/" + gameId + ".conf";

        loadGlobal();
        loadPerGame();

        Logger::debug("Config global:  " + m_globalPath);
        Logger::debug("Config pergame: " + m_gamePath);
    }

    Config::Config(const Config& other) {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_global = other.m_global;
        m_game = other.m_game;
        m_globalPath = other.m_globalPath;
        m_gamePath = other.m_gamePath;
    }

    Config::~Config() {}

    void Config::ensureDirectories() {
        std::error_code ec;
        std::filesystem::create_directories(baseDir() + "/games", ec);
    }

    // Loading
    void Config::loadGlobal() {
        if (!std::filesystem::exists(m_globalPath)) {
            createDefaultGlobal();
            return;
        }
        std::ifstream f(m_globalPath);
        if (f.good()) {
            Logger::info("config file (global): " + m_globalPath);
            readConfigFile(f, m_global);
        }
    }

    void Config::loadPerGame() {
        if (!std::filesystem::exists(m_gamePath)) {
            std::ofstream out(m_gamePath);
            if (out.good()) {
                out << "# vkBasalt-reloaded per-game config\n";
                out << "# Only overridden keys are stored here. Missing keys fall back to the global config.\n";
                out.close();
            }
            return;
        }
        std::ifstream f(m_gamePath);
        if (f.good()) {
            Logger::info("config file (pergame): " + m_gamePath);
            readConfigFile(f, m_game);
        }
    }

    void Config::createDefaultGlobal() {
        std::ofstream out(m_globalPath);
        if (!out.good()) return;
        out << "# vkBasalt-reloaded global config (baseline)\n";
        out << "# Per-game configs override these values.\n\n";
        out << "# Effects (colon-separated): cas, fxaa, smaa, deband, lut, dls,\n";
        out << "#   clarity, clarityrcas, crystalclear, or a ReShade .fx shader name.\n";
        out << "effects = crystalclear\n\n";
        out << "# Enable effects on launch (on/off)\n";
        out << "enableOnLaunch=true\n\n";
        out << "# Keybinds\n";
        out << "toggleKey = Home\n";
        out << "reloadConfigKey = End\n";
        out << "overlayToggleKey = Insert\n\n";
        out << "# Choose a preset: devfav, esports, artifactless, maxsharp, vibrantsharp, devfxaa, cinematic\n";
        out << "crystalclearPreset = devfav\n\n";
        out << "# Depth capture (on/off) - experimental\n";
        out << "depthCapture = off\n\n";
        out << "# Cursor area scale (0 = auto). Only change if mouse pointer is misbehaving.\n";
        out << "cursorScale = 0\n";
        out << "# UI element scale (0 = auto). Scales padding/spacing/widgets, NOT mouse.\n";
        out << "uiScale = 0\n";
        out << "# Theme (hex colors, no # prefix)\n";
        out << "themeBg = 1a0d33\n";
        out << "themeAccent = 47bf59\n";
        out << "themeText = d9f2de\n";
        out << "themeBgAlpha = 0.88\n";
        out << "themeRounding = 3.0\n";
        out << "# Overlay position: left or right\n";
        out << "overlaySide = left\n";
        out.close();
        Logger::info("created default global config at " + m_globalPath);
    }

    void Config::readConfigFile(std::ifstream& stream, std::unordered_map<std::string, std::string>& outMap) {
        std::string line;
        while (std::getline(stream, line)) {
            readConfigLine(line, outMap);
        }
    }

    void Config::readConfigLine(std::string line, std::unordered_map<std::string, std::string>& outMap) {
        std::string key;
        std::string value;

        bool inQuotes    = false;
        bool foundEquals = false;

        auto appendChar = [&key, &value, &foundEquals](const char& newChar) {
            if (foundEquals)
                value += newChar;
            else
                key += newChar;
        };

        for (const char& nextChar : line)
        {
            if (inQuotes)
            {
                if (nextChar == '"')
                    inQuotes = false;
                else
                    appendChar(nextChar);
                continue;
            }
            switch (nextChar)
            {
                case '#': goto BREAK;
                case '"': inQuotes = true; break;
                case '\t':
                case ' ': break;
                case '=': foundEquals = true; break;
                default: appendChar(nextChar); break;
            }
        }

    BREAK:

        key = trim(key);
        value = trim(value);

        if (!key.empty() && !value.empty())
        {
            Logger::info(key + " = " + value);
            outMap[key] = value;
        }
    }

    // Lookup / mutation
    bool Config::findOption(const std::string& option, std::string& outValue) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_game.find(option);
        if (it != m_game.end())   { outValue = it->second; return true; }
        it = m_global.find(option);
        if (it != m_global.end()) { outValue = it->second; return true; }
        return false;
    }

    void Config::setOption(const std::string& option, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game[option] = value;
    }


    void Config::setGlobalOption(const std::string& option, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_global[option] = value;
    }

    bool Config::saveGlobal() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ofstream out(m_globalPath);
        if (!out.good()) return false;
        out << "# vkBasalt-reloaded global config (baseline)\n";
        for (auto& kv : m_global)
            out << kv.first << "=" << kv.second << "\n";
        return true;
    }

    bool Config::savePerGame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ofstream out(m_gamePath);
        if (!out.good()) return false;
        out << "# vkBasalt-reloaded per-game config\n";
        out << "# Only overridden keys are stored here. Missing keys fall back to the global config.\n";
        for (auto& kv : m_game)
            out << kv.first << "=" << kv.second << "\n";
        return true;
    }

    void Config::resetToGlobal() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game.clear();
    }

    // Parsing (preserves old locale-safe behavior)
    void Config::parseOption(const std::string& option, int32_t& result)
    {
        std::string value;
        if (findOption(option, value))
        {
            try
            {
                result = std::stoi(value);
            }
            catch (...)
            {
                Logger::warn("invalid int32_t value for: " + option);
            }
        }
    }

    void Config::parseOption(const std::string& option, float& result)
    {
        std::string value;
        if (findOption(option, value))
        {
            std::stringstream ss(value);
            ss.imbue(std::locale("C"));
            float fvalue;
            ss >> fvalue;

            bool failed = ss.fail();

            std::string rest;
            ss >> rest;
            if (failed || (!rest.empty() && rest != "f"))
            {
                Logger::warn("invalid float value for: " + option);
            }
            else
            {
                result = fvalue;
            }
        }
    }

    void Config::parseOption(const std::string& option, bool& result)
    {
        std::string value;
        if (findOption(option, value))
        {
            if (value == "True" || value == "true" || value == "1" || value == "on" || value == "yes")
            {
                result = true;
            }
            else if (value == "False" || value == "false" || value == "0" || value == "off" || value == "no")
            {
                result = false;
            }
            else
            {
                Logger::warn("invalid bool value for: " + option);
            }
        }
    }

    void Config::parseOption(const std::string& option, std::string& result)
    {
        std::string value;
        if (findOption(option, value))
        {
            result = value;
        }
    }

    void Config::parseOption(const std::string& option, std::vector<std::string>& result)
    {
        std::string value;
        if (findOption(option, value))
        {
            result = {};
            std::stringstream stringStream(value);
            std::string newString;
            while (getline(stringStream, newString, ':'))
            {
                if (!newString.empty())
                    result.push_back(newString);
            }
        }
    }

    // Preset management
    static std::string presetDir() {
        const char* home = std::getenv("HOME");
        std::string root = home ? home : ".";
        return root + "/.config/vkBasalt-reloaded/presets";
    }

    bool Config::savePreset(const std::string& name) {
        if (name.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(presetDir(), ec);
        std::string path = presetDir() + "/" + name + ".conf";
        std::ofstream out(path);
        if (!out.good()) return false;
        out << "# vkBasalt-reloaded preset: " << name << "\n\n";
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_game) {
            out << kv.first << "=" << kv.second << "\n";
        }
        Logger::info("Saved preset: " + path);
        return true;
    }

    bool Config::loadPreset(const std::string& name) {
        if (name.empty()) return false;
        std::string path = presetDir() + "/" + name + ".conf";
        std::ifstream f(path);
        if (!f.good()) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game.clear();
        readConfigFile(f, m_game);
        Logger::info("Loaded preset: " + path);
        return true;
    }

    bool Config::deletePreset(const std::string& name) {
        if (name.empty()) return false;
        std::string path = presetDir() + "/" + name + ".conf";
        std::error_code ec;
        bool removed = std::filesystem::remove(path, ec);
        if (removed) Logger::info("Deleted preset: " + path);
        return removed;
    }

    std::vector<std::string> Config::listPresets() {
        std::vector<std::string> result;
        try {
        std::string dir = presetDir();
        if (!std::filesystem::exists(dir)) return result;
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".conf") {
                    result.push_back(filename.substr(0, filename.size() - 5));
                }
            }
        }
        std::sort(result.begin(), result.end());
        } catch (...) {
            // Filesystem error, return empty list
        }
        return result;
    }

} // namespace vkBasalt
