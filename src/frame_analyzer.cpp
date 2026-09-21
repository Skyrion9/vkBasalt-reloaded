#include "frame_analyzer.hpp"

#include <cstring>
#include <iterator>

#include "format.hpp"
#include "logger.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    FrameAnalyzer::FrameAnalyzer(
        LogicalDevice* pDevice,
        VkExtent2D extent,
        const std::vector<VkImage>& inputImages,
        VkFormat inputFormat,
        VkColorSpaceKHR colorSpace) :
        m_pDevice(pDevice), m_extent(extent), m_inputImages(inputImages),
        m_colorSpaceMode(static_cast<int>(getColorSpaceMode(inputFormat, colorSpace)))
    {
        m_pushConstants = {0};

        m_specData       = {.width = extent.width, .height = extent.height, .colorSpaceMode = m_colorSpaceMode};
        m_specMapEntries = {
            {.constantID = 0, .offset = offsetof(SpecData, width), .size = sizeof(uint32_t)},
            {.constantID = 1, .offset = offsetof(SpecData, height), .size = sizeof(uint32_t)},
            {.constantID = 65535, .offset = offsetof(SpecData, colorSpaceMode), .size = sizeof(int32_t)}};
        m_specInfo               = {};
        m_specInfo.mapEntryCount = static_cast<uint32_t>(m_specMapEntries.size());
        m_specInfo.pMapEntries   = m_specMapEntries.data();
        m_specInfo.dataSize      = sizeof(SpecData);
        m_specInfo.pData         = &m_specData;

        createResources();
    }

    FrameAnalyzer::~FrameAnalyzer()
    {
        destroyResources();
    }

    void FrameAnalyzer::createResources()
    {
        auto& vkd = m_pDevice->vkd;
        auto dev  = m_pDevice->device;

        // SSBOs
        m_histBuffer = SimpleComputePass::createDeviceLocalBuffer(
            m_pDevice, HIST_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_histMemory);
        m_waveBuffer = SimpleComputePass::createDeviceLocalBuffer(
            m_pDevice, SCOPE_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_waveMemory);
        m_vecBuffer = SimpleComputePass::createDeviceLocalBuffer(
            m_pDevice, SCOPE_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_vecMemory);

        // Active flag buffer
        {
            VkBufferCreateInfo activeBufInfo = {};
            activeBufInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            activeBufInfo.size               = 16;
            activeBufInfo.usage              = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            vkd.CreateBuffer(dev, &activeBufInfo, nullptr, &m_activeBuffer);

            VkMemoryRequirements memReqs;
            vkd.GetBufferMemoryRequirements(dev, m_activeBuffer, &memReqs);

            VkPhysicalDeviceMemoryProperties memProps;
            m_pDevice->vki.GetPhysicalDeviceMemoryProperties(m_pDevice->physicalDevice, &memProps);

            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize       = memReqs.size;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if ((memReqs.memoryTypeBits & (1 << i))
                    && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                    && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    allocInfo.memoryTypeIndex = i;
                    break;
                }
            }
            vkd.AllocateMemory(dev, &allocInfo, nullptr, &m_activeMemory);
            vkd.BindBufferMemory(dev, m_activeBuffer, m_activeMemory, 0);
            vkd.MapMemory(dev, m_activeMemory, 0, 16, 0, reinterpret_cast<void**>(&m_mappedActive));
            *m_mappedActive = (m_enabled && m_overlayVisible) ? 1 : 0;
        }

        // Output images
        for (int i = 0; i < SCOPE_COUNT; i++) {
            m_scopeImages[i] = SimpleComputePass::createImage(
                m_pDevice, SCOPE_DIM, SCOPE_DIM, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_scopeMemory[i]);
            m_scopeViews[i] = SimpleComputePass::createImageView(m_pDevice, m_scopeImages[i], VK_FORMAT_R8G8B8A8_UNORM);
        }

        // Input image views + sampler
        m_inputViews.resize(m_inputImages.size());
        for (size_t i = 0; i < m_inputImages.size(); i++)
            m_inputViews[i] = SimpleComputePass::createImageView(
                m_pDevice, m_inputImages[i],
                m_pDevice->swapchainFormat != VK_FORMAT_UNDEFINED ? m_pDevice->swapchainFormat
                                                                  : VK_FORMAT_B8G8R8A8_UNORM);

        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter           = VK_FILTER_LINEAR;
        samplerInfo.minFilter           = VK_FILTER_LINEAR;
        samplerInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod              = 1.0f;
        vkd.CreateSampler(dev, &samplerInfo, nullptr, &m_sampler);

        // Accumulate DSL
        VkDescriptorSetLayoutBinding accumBindings[] = {
            {.binding            = 0,
             .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 1,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 2,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 3,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 4,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr}, // active flag
        };
        VkDescriptorSetLayoutCreateInfo accumDSLInfo = {};
        accumDSLInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        accumDSLInfo.bindingCount                    = std::size(accumBindings);
        accumDSLInfo.pBindings                       = accumBindings;
        vkd.CreateDescriptorSetLayout(dev, &accumDSLInfo, nullptr, &m_accumDSL);

        // Resolve DSL
        VkDescriptorSetLayoutBinding resolveBindings[] = {
            {.binding            = 0,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 1,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 2,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 3,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 4,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 5,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding            = 6,
             .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount    = 1,
             .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr}, // active flag
        };
        VkDescriptorSetLayoutCreateInfo resolveDSLInfo = {};
        resolveDSLInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        resolveDSLInfo.bindingCount                    = std::size(resolveBindings);
        resolveDSLInfo.pBindings                       = resolveBindings;
        vkd.CreateDescriptorSetLayout(dev, &resolveDSLInfo, nullptr, &m_resolveDSL);

        // Pipeline layouts
        VkPushConstantRange pcRange = {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(uint32_t)};

        VkPipelineLayoutCreateInfo plInfo = {};
        plInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount             = 1;
        plInfo.pSetLayouts                = &m_accumDSL;
        plInfo.pushConstantRangeCount     = 1;
        plInfo.pPushConstantRanges        = &pcRange;
        vkd.CreatePipelineLayout(dev, &plInfo, nullptr, &m_accumLayout);

        plInfo.pSetLayouts = &m_resolveDSL;
        vkd.CreatePipelineLayout(dev, &plInfo, nullptr, &m_resolveLayout);

        // Shader modules
        auto createModule = [&](const std::vector<uint32_t>& code, VkShaderModule& mod) {
            VkShaderModuleCreateInfo smInfo = {};
            smInfo.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smInfo.codeSize                 = code.size() * sizeof(uint32_t);
            smInfo.pCode                    = code.data();
            vkd.CreateShaderModule(dev, &smInfo, nullptr, &mod);
        };

        createModule(decompressShaderCached(frame_accumulate_comp), m_accumShader);
        createModule(decompressShaderCached(frame_resolve_comp), m_resolveShader);

        // Pipelines
        auto createPipeline = [&](VkShaderModule mod, VkPipelineLayout layout, VkPipeline& pipe) {
            VkComputePipelineCreateInfo cpInfo = {};
            cpInfo.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpInfo.stage.sType                 = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module                = mod;
            cpInfo.stage.pName                 = "main";
            cpInfo.stage.pSpecializationInfo   = &m_specInfo;
            cpInfo.layout                      = layout;
            vkd.CreateComputePipelines(dev, m_pDevice->pipelineCache, 1, &cpInfo, nullptr, &pipe);
        };
        createPipeline(m_accumShader, m_accumLayout, m_accumPipeline);
        createPipeline(m_resolveShader, m_resolveLayout, m_resolvePipeline);

        // Descriptor pool
        auto imgCount = static_cast<uint32_t>(m_inputImages.size());

        uint32_t totalSamplers = 0, totalSSBOs = 0, totalStorageImages = 0;
        for (const auto& b : accumBindings) {
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) totalSamplers += imgCount;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) totalSSBOs += imgCount;
        }
        for (const auto& b : resolveBindings) {
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) totalSSBOs += 1;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) totalStorageImages += 1;
        }

        VkDescriptorPoolSize poolSizes[] = {
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = totalSamplers},
            {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = totalSSBOs},
            {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = totalStorageImages},
        };
        VkDescriptorPoolCreateInfo dpInfo = {};
        dpInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpInfo.maxSets                    = imgCount + 1; // accumulate per image + 1 resolve
        dpInfo.poolSizeCount              = std::size(poolSizes);
        dpInfo.pPoolSizes                 = poolSizes;
        vkd.CreateDescriptorPool(dev, &dpInfo, nullptr, &m_pool);

        // Allocate descriptor sets accumulate one per image
        m_accumSets.resize(imgCount);
        {
            std::vector<VkDescriptorSetLayout> layouts(imgCount, m_accumDSL);
            VkDescriptorSetAllocateInfo ai = {};
            ai.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool              = m_pool;
            ai.descriptorSetCount          = imgCount;
            ai.pSetLayouts                 = layouts.data();
            vkd.AllocateDescriptorSets(dev, &ai, m_accumSets.data());
        }

        // Resolve: one set
        {
            VkDescriptorSetAllocateInfo ai = {};
            ai.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool              = m_pool;
            ai.descriptorSetCount          = 1;
            ai.pSetLayouts                 = &m_resolveDSL;
            vkd.AllocateDescriptorSets(dev, &ai, &m_resolveSet);
        }

        // Shared buffer infos for descriptor writes
        VkDescriptorBufferInfo histBuf   = {.buffer = m_histBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkDescriptorBufferInfo waveBuf   = {.buffer = m_waveBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkDescriptorBufferInfo vecBuf    = {.buffer = m_vecBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkDescriptorBufferInfo activeBuf = {.buffer = m_activeBuffer, .offset = 0, .range = VK_WHOLE_SIZE};

        // Write accumulate descriptors
        for (uint32_t i = 0; i < imgCount; i++) {
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.sampler               = m_sampler;
            imgInfo.imageView             = m_inputViews[i];
            imgInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[] = {
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_accumSets[i],
                 .dstBinding      = 0,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .pImageInfo      = &imgInfo},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_accumSets[i],
                 .dstBinding      = 1,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &histBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_accumSets[i],
                 .dstBinding      = 2,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &waveBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_accumSets[i],
                 .dstBinding      = 3,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &vecBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_accumSets[i],
                 .dstBinding      = 4,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &activeBuf},
            };
            vkd.UpdateDescriptorSets(dev, std::size(writes), writes, 0, nullptr);
        }

        // Write resolve descriptors
        {
            VkDescriptorImageInfo histImg = {
                .sampler     = VK_NULL_HANDLE,
                .imageView   = m_scopeViews[HISTOGRAM],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo waveImg = {
                .sampler = VK_NULL_HANDLE, .imageView = m_scopeViews[WAVEFORM], .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo vecImg = {
                .sampler     = VK_NULL_HANDLE,
                .imageView   = m_scopeViews[VECTORSCOPE],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL};

            VkWriteDescriptorSet writes[] = {
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 0,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &histBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 1,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &waveBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 2,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &vecBuf},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 3,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo      = &histImg},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 4,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo      = &waveImg},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 5,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo      = &vecImg},
                {.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext           = nullptr,
                 .dstSet          = m_resolveSet,
                 .dstBinding      = 6,
                 .dstArrayElement = 0,
                 .descriptorCount = 1,
                 .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo      = nullptr,
                 .pBufferInfo     = &activeBuf},
            };
            vkd.UpdateDescriptorSets(dev, std::size(writes), writes, 0, nullptr);
        }

        // One time GPU setup: transition scope images from UNDEFINED to GENERAL to guarantee it runs once,
        // regardless of which swapchain image's command buffer is recorded or submitted first.
        {
            VkCommandBufferAllocateInfo cmdAllocInfo = {};
            cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool                 = m_pDevice->commandPool;
            cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount          = 1;

            VkCommandBuffer setupCmd = VK_NULL_HANDLE;
            vkd.AllocateCommandBuffers(dev, &cmdAllocInfo, &setupCmd);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkd.BeginCommandBuffer(setupCmd, &beginInfo);

            for (int i = 0; i < SCOPE_COUNT; i++) {
                VkImageMemoryBarrier barrier = {};
                barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask        = 0;
                barrier.dstAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                = m_scopeImages[i];
                barrier.subresourceRange     = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1};
                vkd.CmdPipelineBarrier(
                    setupCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                    nullptr, 1, &barrier);
            }

            vkd.EndCommandBuffer(setupCmd);

            VkFenceCreateInfo fenceInfo = {};
            fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence setupFence          = VK_NULL_HANDLE;
            vkd.CreateFence(dev, &fenceInfo, nullptr, &setupFence);

            VkSubmitInfo submitInfo       = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &setupCmd;
            vkd.QueueSubmit(m_pDevice->queue, 1, &submitInfo, setupFence);

            vkd.WaitForFences(dev, 1, &setupFence, VK_TRUE, UINT64_MAX);

            vkd.DestroyFence(dev, setupFence, nullptr);
            vkd.FreeCommandBuffers(dev, m_pDevice->commandPool, 1, &setupCmd);
        }

        Logger::debug("FrameAnalyzer resources created");
    }

    void FrameAnalyzer::recordCommands(VkCommandBuffer cmdBuf, uint32_t imageIndex)
    {
        if (imageIndex >= m_accumSets.size()) return;
        auto& vkd = m_pDevice->vkd;

        m_pushConstants.enabled = 1;

        // 1. Clear SSBOs
        vkd.CmdFillBuffer(cmdBuf, m_histBuffer, 0, VK_WHOLE_SIZE, 0);
        vkd.CmdFillBuffer(cmdBuf, m_waveBuffer, 0, VK_WHOLE_SIZE, 0);
        vkd.CmdFillBuffer(cmdBuf, m_vecBuffer, 0, VK_WHOLE_SIZE, 0);

        // Barrier: TRANSFER_WRITE -> SHADER_RW
        {
            VkMemoryBarrier memBarrier = {};
            memBarrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
            memBarrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkd.CmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0,
                nullptr, 0, nullptr);
        }

        // 2. Accumulate pass
        vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumPipeline);
        vkd.CmdBindDescriptorSets(
            cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumLayout, 0, 1, &m_accumSets[imageIndex], 0, nullptr);
        vkd.CmdPushConstants(cmdBuf, m_accumLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &m_pushConstants);
        vkd.CmdDispatch(cmdBuf, (m_extent.width + 15) / 16, (m_extent.height + 15) / 16, 1);

        // Barrier: SHADER_WRITE -> SHADER_RW (SSBO accumulate -> SSBO resolve read + image write)
        {
            VkMemoryBarrier memBarrier = {};
            memBarrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkd.CmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier,
                0, nullptr, 0, nullptr);
        }

        // 3. Resolve pass
        vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolvePipeline);
        vkd.CmdBindDescriptorSets(
            cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveLayout, 0, 1, &m_resolveSet, 0, nullptr);
        vkd.CmdPushConstants(
            cmdBuf, m_resolveLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &m_pushConstants);
        vkd.CmdDispatch(cmdBuf, (SCOPE_DIM + 15) / 16, (SCOPE_DIM + 15) / 16, 1);

        // 4. Synchronization Barrier: Compute Write -> Fragment Read (ImGui) Without this scopes turns into sliding puzzles due to race conditions.
        {
            VkImageMemoryBarrier barriers[SCOPE_COUNT] = {};
            for (int i = 0; i < SCOPE_COUNT; i++) {
                barriers[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[i].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
                barriers[i].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                barriers[i].oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
                barriers[i].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
                barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].image               = m_scopeImages[i];
                barriers[i].subresourceRange    = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1};
            }
            vkd.CmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                nullptr, SCOPE_COUNT, barriers);
        }
    }

    void FrameAnalyzer::destroyResources()
    {
        auto& vkd = m_pDevice->vkd;
        auto dev  = m_pDevice->device;

        if (m_mappedActive) vkd.UnmapMemory(dev, m_activeMemory);
        if (m_activeBuffer) vkd.DestroyBuffer(dev, m_activeBuffer, nullptr);
        if (m_activeMemory) vkd.FreeMemory(dev, m_activeMemory, nullptr);
        if (m_pool) vkd.DestroyDescriptorPool(dev, m_pool, nullptr);
        if (m_sampler) vkd.DestroySampler(dev, m_sampler, nullptr);
        if (m_accumPipeline) vkd.DestroyPipeline(dev, m_accumPipeline, nullptr);
        if (m_resolvePipeline) vkd.DestroyPipeline(dev, m_resolvePipeline, nullptr);
        if (m_accumLayout) vkd.DestroyPipelineLayout(dev, m_accumLayout, nullptr);
        if (m_resolveLayout) vkd.DestroyPipelineLayout(dev, m_resolveLayout, nullptr);
        if (m_accumDSL) vkd.DestroyDescriptorSetLayout(dev, m_accumDSL, nullptr);
        if (m_resolveDSL) vkd.DestroyDescriptorSetLayout(dev, m_resolveDSL, nullptr);
        if (m_accumShader) vkd.DestroyShaderModule(dev, m_accumShader, nullptr);
        if (m_resolveShader) vkd.DestroyShaderModule(dev, m_resolveShader, nullptr);

        for (auto v : m_inputViews)
            if (v) vkd.DestroyImageView(dev, v, nullptr);
        for (int i = 0; i < SCOPE_COUNT; i++) {
            if (m_scopeViews[i]) vkd.DestroyImageView(dev, m_scopeViews[i], nullptr);
            if (m_scopeImages[i]) vkd.DestroyImage(dev, m_scopeImages[i], nullptr);
            if (m_scopeMemory[i]) vkd.FreeMemory(dev, m_scopeMemory[i], nullptr);
        }
        if (m_histBuffer) vkd.DestroyBuffer(dev, m_histBuffer, nullptr);
        if (m_waveBuffer) vkd.DestroyBuffer(dev, m_waveBuffer, nullptr);
        if (m_vecBuffer) vkd.DestroyBuffer(dev, m_vecBuffer, nullptr);
        if (m_histMemory) vkd.FreeMemory(dev, m_histMemory, nullptr);
        if (m_waveMemory) vkd.FreeMemory(dev, m_waveMemory, nullptr);
        if (m_vecMemory) vkd.FreeMemory(dev, m_vecMemory, nullptr);
    }
} // namespace vkBasalt
