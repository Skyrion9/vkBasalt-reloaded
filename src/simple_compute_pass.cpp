#include "simple_compute_pass.hpp"
#include "logical_device.hpp"
#include "logger.hpp"
#include <cstring>

namespace vkBasalt
{
    SimpleComputePass::SimpleComputePass(LogicalDevice* pDevice)
        : pLogicalDevice(pDevice)
    {
    }

    SimpleComputePass::~SimpleComputePass()
    {
        onDestroy();

        if (pipeline != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, pipeline, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        if (descriptorPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);
        if (descriptorSetLayout != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, descriptorSetLayout, nullptr);
        if (shaderModule != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, shaderModule, nullptr);
    }

    static uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& props,
                                   uint32_t typeBits, VkMemoryPropertyFlags required)
    {
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        {
            if ((typeBits & (1 << i)) &&
                (props.memoryTypes[i].propertyFlags & required) == required)
                return i;
        }
        return 0;
    }

    VkBuffer SimpleComputePass::createDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer = VK_NULL_HANDLE;
        VkResult result = pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &info, nullptr, &buffer);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreateBuffer failed"); return VK_NULL_HANDLE; }

        VkMemoryRequirements memReqs;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(pLogicalDevice->memoryProperties,
                                                   memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory memory = VK_NULL_HANDLE;
        result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &memory);
        if (result != VK_SUCCESS) { pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr); return VK_NULL_HANDLE; }

        pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, memory, 0);
        return buffer;
    }

    VkBuffer SimpleComputePass::createHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                        VkDeviceMemory& memory, void** mapped)
    {
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer = VK_NULL_HANDLE;
        VkResult result = pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &info, nullptr, &buffer);
        if (result != VK_SUCCESS) return VK_NULL_HANDLE;

        VkMemoryRequirements memReqs;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(pLogicalDevice->memoryProperties,
                                                   memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &memory);
        if (result != VK_SUCCESS) { pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr); return VK_NULL_HANDLE; }

        pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, memory, 0);
        if (mapped) pLogicalDevice->vkd.MapMemory(pLogicalDevice->device, memory, 0, size, 0, mapped);
        return buffer;
    }

    VkImage SimpleComputePass::createImage(uint32_t width, uint32_t height, VkFormat format,
                                           VkImageUsageFlags usage, VkDeviceMemory& memory)
    {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage image = VK_NULL_HANDLE;
        VkResult result = pLogicalDevice->vkd.CreateImage(pLogicalDevice->device, &info, nullptr, &image);
        if (result != VK_SUCCESS) return VK_NULL_HANDLE;

        VkMemoryRequirements memReqs;
        pLogicalDevice->vkd.GetImageMemoryRequirements(pLogicalDevice->device, image, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(pLogicalDevice->memoryProperties,
                                                   memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &memory);
        if (result != VK_SUCCESS) { pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr); return VK_NULL_HANDLE; }

        pLogicalDevice->vkd.BindImageMemory(pLogicalDevice->device, image, memory, 0);
        return image;
    }

    VkImageView SimpleComputePass::createImageView(VkImage image, VkFormat format,
                                                   VkImageViewType viewType, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = viewType;
        info.format = format;
        info.subresourceRange = {aspect, 0, 1, 0, 1};

        VkImageView view = VK_NULL_HANDLE;
        pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &info, nullptr, &view);
        return view;
    }

    void SimpleComputePass::transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
                                                  VkImageLayout oldLayout, VkImageLayout newLayout,
                                                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                                  VkAccessFlags srcAccess, VkAccessFlags dstAccess)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(cmdBuf, srcStage, dstStage,
                                               0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void SimpleComputePass::init()
    {
        // Shader module
        const auto& code = getShaderCode();
        VkShaderModuleCreateInfo smInfo = {};
        smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smInfo.codeSize = code.size() * sizeof(uint32_t);
        smInfo.pCode = code.data();
        VkResult result = pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &smInfo, nullptr, &shaderModule);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreateShaderModule failed"); return; }

        // Descriptor set layout
        auto bindings = getBindings();
        VkDescriptorSetLayoutCreateInfo dslInfo = {};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = (uint32_t)bindings.size();
        dslInfo.pBindings = bindings.data();
        result = pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &dslInfo, nullptr, &descriptorSetLayout);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreateDescriptorSetLayout failed"); return; }

        // Pipeline layout (with push constants if provided)
        pushConstantSize = getPushConstantSize();
        VkPipelineLayoutCreateInfo plInfo = {};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &descriptorSetLayout;
        if (pushConstantSize > 0)
        {
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = pushConstantSize;
            plInfo.pushConstantRangeCount = 1;
            plInfo.pPushConstantRanges = &pushConstantRange;
        }
        result = pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &plInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreatePipelineLayout failed"); return; }

        // Compute pipeline
        VkComputePipelineCreateInfo cpInfo = {};
        cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = shaderModule;
        cpInfo.stage.pName = "main";
        cpInfo.layout = pipelineLayout;
        result = pLogicalDevice->vkd.CreateComputePipelines(pLogicalDevice->device,
                                                            pLogicalDevice->pipelineCache,
                                                            1, &cpInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreateComputePipelines failed"); return; }

        // Descriptor pool + sets (one per swapchain image)
        uint32_t imageCount = 8; // safe default, subclasses with per image sets resize below
        VkDescriptorPoolCreateInfo dpInfo = {};
        dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpInfo.maxSets = imageCount;
        // Aggregate pool sizes from bindings
        std::vector<VkDescriptorPoolSize> poolSizes;
        for (const auto& b : bindings)
        {
            bool found = false;
            for (auto& ps : poolSizes)
            {
                if (ps.type == b.descriptorType) { ps.descriptorCount += imageCount; found = true; break; }
            }
            if (!found) poolSizes.push_back({b.descriptorType, imageCount});
        }
        dpInfo.poolSizeCount = (uint32_t)poolSizes.size();
        dpInfo.pPoolSizes = poolSizes.data();
        result = pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &dpInfo, nullptr, &descriptorPool);
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: CreateDescriptorPool failed"); return; }

        descriptorSets.resize(imageCount);
        std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);
        VkDescriptorSetAllocateInfo dsInfo = {};
        dsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsInfo.descriptorPool = descriptorPool;
        dsInfo.descriptorSetCount = imageCount;
        dsInfo.pSetLayouts = layouts.data();
        result = pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &dsInfo, descriptorSets.data());
        if (result != VK_SUCCESS) { Logger::err("SimpleComputePass: AllocateDescriptorSets failed"); return; }

        // Subclass specific init (create images, buffers, transition layouts, etc.)
        onInit();

        // Write descriptors for each image index
        for (uint32_t i = 0; i < imageCount; i++)
            writeDescriptors(descriptorSets[i], i);

        Logger::debug("SimpleComputePass initialized: " + getName());
    }

    // Recording
    void SimpleComputePass::recordCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!m_enabled || pipeline == VK_NULL_HANDLE) return;
        if (imageIndex >= descriptorSets.size()) return;

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                  pipelineLayout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);
        if (pushConstantSize > 0)
        {
            const void* pc = getPushConstants();
            if (pc)
                pLogicalDevice->vkd.CmdPushConstants(commandBuffer, pipelineLayout,
                                                     VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize, pc);
        }

        uint32_t x = 1, y = 1, z = 1;
        getDispatchSize(x, y, z);
        pLogicalDevice->vkd.CmdDispatch(commandBuffer, x, y, z);
    }
} // namespace vkBasalt
