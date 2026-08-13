#include "buffer.hpp"
#include "logical_device.hpp"
#include "memory.hpp"
#include "vulkan_include.hpp"
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    void createBuffer(LogicalDevice*        pLogicalDevice,
                    VkDeviceSize          size,
                    VkBufferUsageFlags    usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer&             buffer,
                    VkDeviceMemory&       bufferMemory)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = size;
        bufferInfo.usage       = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &bufferInfo, nullptr, &buffer);
        ASSERT_VULKAN(result);

        VkMemoryRequirements memRequirements;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryTypeIndex(pLogicalDevice, memRequirements.memoryTypeBits, properties);
        result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &bufferMemory);
        if (result != VK_SUCCESS) {
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            ASSERT_VULKAN(result);
        }

        result = pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, bufferMemory, 0);
        if (result != VK_SUCCESS) {
            pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, bufferMemory, nullptr);
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            bufferMemory = VK_NULL_HANDLE;
            ASSERT_VULKAN(result);
        }
    }
} // namespace vkBasalt
