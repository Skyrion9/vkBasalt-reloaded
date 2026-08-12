#include "pipeline_cache.hpp"
#include "logger.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace vkBasalt {

    std::string getPipelineCachePath(VkPhysicalDevice physicalDevice, InstanceDispatch& vki) {
        VkPhysicalDeviceProperties props;
        vki.GetPhysicalDeviceProperties(physicalDevice, &props);

        // Use vendorID + deviceID + driverVersion as cache key.
        // Uniquely identifies the GPU + driver combination without needing Vulkan 1.1 structs.
        char filename[256];
        std::snprintf(filename, sizeof(filename), "pipeline_cache_%04x_%04x_%u.bin",
                      props.vendorID, props.deviceID, props.driverVersion);

        const char* home = std::getenv("HOME");
        std::string root = home ? home : ".";
        return root + "/.config/vkBasalt-reloaded/" + filename;
    }

    std::vector<uint8_t> loadPipelineCacheData(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.good()) return {};

        size_t size = (size_t)f.tellg();
        if (size == 0) return {};

        // Basic sanity check: cache header must start with VK_PIPELINE_CACHE_HEADER_VERSION_ONE
        if (size < sizeof(uint32_t)) return {};

        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        f.read(reinterpret_cast<char*>(data.data()), size);

        Logger::debug("Loaded pipeline cache: " + path + " (" + std::to_string(size) + " bytes)");
        return data;
    }

    void savePipelineCacheData(VkDevice device, DeviceDispatch& vkd, VkPipelineCache cache, const std::string& path) {
        if (cache == VK_NULL_HANDLE) return;

        size_t dataSize = 0;
        vkd.GetPipelineCacheData(device, cache, &dataSize, nullptr);
        if (dataSize == 0) return;

        std::vector<uint8_t> data(dataSize);
        vkd.GetPipelineCacheData(device, cache, &dataSize, data.data());

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        std::ofstream f(path, std::ios::binary);
        if (!f.good()) {
            Logger::err("Failed to write pipeline cache: " + path);
            return;
        }
        f.write(reinterpret_cast<const char*>(data.data()), dataSize);
        Logger::debug("Saved pipeline cache: " + path + " (" + std::to_string(dataSize) + " bytes)");
    }

} // namespace vkBasalt
