#include "shader.hpp"
#include "logical_device.hpp"
#include "vulkan_include.hpp"

#include <cstdint>
#include <cstring>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    void createShaderModule(LogicalDevice* pLogicalDevice, const std::vector<char>& code, VkShaderModule* shaderModule)
    {
        // Ensure 4-byte alignment for SPIR-V. Copy if the source buffer is misaligned.
        const uint32_t* spirvData;
        std::vector<uint32_t> alignedCopy;
        if (reinterpret_cast<uintptr_t>(code.data()) % alignof(uint32_t) == 0) {
            spirvData = reinterpret_cast<const uint32_t*>(code.data());
        } else {
            alignedCopy.resize(code.size() / sizeof(uint32_t));
            std::memcpy(alignedCopy.data(), code.data(), code.size());
            spirvData = alignedCopy.data();
        }

        VkShaderModuleCreateInfo shaderCreateInfo;
        shaderCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderCreateInfo.pNext    = nullptr;
        shaderCreateInfo.flags    = 0;
        shaderCreateInfo.codeSize = code.size();
        shaderCreateInfo.pCode    = spirvData;
        VkResult result = pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &shaderCreateInfo, nullptr, shaderModule);
        ASSERT_VULKAN(result);
    }

    void createShaderModule(LogicalDevice* pLogicalDevice, const std::vector<uint32_t>& code, VkShaderModule* shaderModule)
    {
        VkShaderModuleCreateInfo shaderCreateInfo;

        shaderCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderCreateInfo.pNext    = nullptr;
        shaderCreateInfo.flags    = 0;
        shaderCreateInfo.codeSize = code.size() * sizeof(uint32_t);
        shaderCreateInfo.pCode    = code.data();

        VkResult result = pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &shaderCreateInfo, nullptr, shaderModule);
        ASSERT_VULKAN(result);
    }
} // namespace vkBasalt
