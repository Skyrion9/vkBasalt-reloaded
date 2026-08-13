#include "memory.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "vulkan_include.hpp"
#include <cstdint>
#include <string>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    uint32_t findMemoryTypeIndex(LogicalDevice* pLogicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        // Use cached memory properties instead of querying the driver every time
        const auto& props = pLogicalDevice->memoryProperties;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (props.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        Logger::err("Found no correct memory type (filter=0x" + std::to_string(typeFilter) + ", props=0x" + std::to_string(properties) + ")");
        ASSERT_VULKAN(VK_ERROR_FEATURE_NOT_PRESENT); // Hard fail instead of returning garbage index
        return 0;
    }
} // namespace vkBasalt
