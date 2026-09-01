#ifndef CONFIG_HPP_INCLUDED
#define CONFIG_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <unordered_map>
#include <cstdlib>
#include <mutex>
#include <atomic>

namespace vkBasalt
{
    extern std::atomic<bool> g_configDirty;

    class Config
    {
    public:
        Config();
        Config(const Config& other);
        ~Config();

        template<typename T>
        T getOption(const std::string& option, const T& defaultValue = {})
        {
            T result = defaultValue;
            parseOption(option, result);
            return result;
        }

        void setOption(const std::string& option, const std::string& value);
        void setGlobalOption(const std::string& option, const std::string& value);
        bool saveGlobal();

        bool savePerGame();
        bool hasPerGameOption(const std::string& option) const;
        void removePerGameOption(const std::string& option);
        bool hasPerGameOverrides() const;
        std::string getGlobalPath() const { return m_globalPath; }
        std::string getPerGamePath() const { return m_gamePath; }
        std::string getGamePath() const { return m_gamePath; }

        // Reset per-game vars to global config's.
        void resetToGlobal();

        bool savePreset(const std::string& name);
        bool loadPreset(const std::string& name);
        bool deletePreset(const std::string& name);
        std::vector<std::string> listPresets();

    private:
        std::unordered_map<std::string, std::string> m_global;
        std::unordered_map<std::string, std::string> m_game;

        std::string m_globalPath;
        std::string m_gamePath;

        mutable std::mutex m_mutex;

        void ensureDirectories();
        void loadGlobal();
        void loadPerGame();
        void createDefaultGlobal();
        void readConfigFile(std::ifstream& stream, std::unordered_map<std::string, std::string>& outMap);
        void readConfigLine(std::string line, std::unordered_map<std::string, std::string>& outMap);

        // Per-game -> global lookup, returns false if not found.
        bool findOption(const std::string& option, std::string& outValue) const;

        void parseOption(const std::string& option, int32_t& result);
        void parseOption(const std::string& option, float& result);
        void parseOption(const std::string& option, bool& result);
        void parseOption(const std::string& option, std::string& result);
        void parseOption(const std::string& option, std::vector<std::string>& result);
    };
} // namespace vkBasalt

#endif // CONFIG_HPP_INCLUDED
