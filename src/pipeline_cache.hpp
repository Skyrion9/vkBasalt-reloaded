#pragma once

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"
#include <string>
#include <vector>

namespace vkBasalt {
    // Generate a unique cache path based on GPU UUID + driver version. Automatically invalidates when the driver is updated.
    std::string getPipelineCachePath(VkPhysicalDevice physicalDevice, InstanceDispatch& vki);

    // Load serialized cache data from disk. Returns empty vector if missing/corrupt.
    std::vector<uint8_t> loadPipelineCacheData(const std::string& path);

    // Serialize and save cache data to disk.
    void savePipelineCacheData(VkDevice device, DeviceDispatch& vkd, VkPipelineCache cache, const std::string& path);
} // namespace vkBasalt
