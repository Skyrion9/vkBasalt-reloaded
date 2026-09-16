#include "frame_analyzer.hpp"

#include <cstring>
#include <iterator>

#include "format.hpp"
#include "logger.hpp"
#include "shader_sources.hpp"


namespace vkBasalt
{
    FrameAnalyzer::FrameAnalyzer(LogicalDevice* pDevice, VkExtent2D extent,
                                 const std::vector<VkImage>& inputImages, VkFormat inputFormat,
                                 VkColorSpaceKHR colorSpace)
        : m_pDevice(pDevice)
        , m_extent(extent)
        , m_inputImages(inputImages)
    {
        m_colorSpaceMode = static_cast<int>(getColorSpaceMode(inputFormat, colorSpace));
        m_pushConstants = { extent.width, extent.height, 0};
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
        m_histBuffer = SimpleComputePass::createDeviceLocalBuffer(m_pDevice, HIST_SIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_histMemory);
        m_waveBuffer = SimpleComputePass::createDeviceLocalBuffer(m_pDevice, SCOPE_SIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_waveMemory);
        m_vecBuffer  = SimpleComputePass::createDeviceLocalBuffer(m_pDevice, SCOPE_SIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_vecMemory);

        // Active flag buffer
        {
            VkBufferCreateInfo activeBufInfo = {};
            activeBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            activeBufInfo.size = 16;
            activeBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            vkd.CreateBuffer(dev, &activeBufInfo, nullptr, &m_activeBuffer);

            VkMemoryRequirements memReqs;
            vkd.GetBufferMemoryRequirements(dev, m_activeBuffer, &memReqs);
            
            VkPhysicalDeviceMemoryProperties memProps;
            m_pDevice->vki.GetPhysicalDeviceMemoryProperties(m_pDevice->physicalDevice, &memProps);
            
            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if ((memReqs.memoryTypeBits & (1 << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                    (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    allocInfo.memoryTypeIndex = i;
                    break;
                }
            }
            vkd.AllocateMemory(dev, &allocInfo, nullptr, &m_activeMemory);
            vkd.BindBufferMemory(dev, m_activeBuffer, m_activeMemory, 0);
            vkd.MapMemory(dev, m_activeMemory, 0, 16, 0, (void**)&m_mappedActive);
            *m_mappedActive = (m_enabled && m_overlayVisible) ? 1 : 0;
        }

        // Output images
        for (int i = 0; i < SCOPE_COUNT; i++)
        {
            m_scopeImages[i] = SimpleComputePass::createImage(m_pDevice, SCOPE_DIM, SCOPE_DIM,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_scopeMemory[i]);
            m_scopeViews[i] = SimpleComputePass::createImageView(m_pDevice, m_scopeImages[i],
                VK_FORMAT_R8G8B8A8_UNORM);
        }

        // Input image views + sampler
        m_inputViews.resize(m_inputImages.size());
        for (size_t i = 0; i < m_inputImages.size(); i++)
            m_inputViews[i] = SimpleComputePass::createImageView(m_pDevice, m_inputImages[i],
                m_pDevice->swapchainFormat != VK_FORMAT_UNDEFINED ? m_pDevice->swapchainFormat : VK_FORMAT_B8G8R8A8_UNORM);

        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        vkd.CreateSampler(dev, &samplerInfo, nullptr, &m_sampler);

        // Accumulate DSL
        VkDescriptorSetLayoutBinding accumBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // active flag
        };
        VkDescriptorSetLayoutCreateInfo accumDSLInfo = {};
        accumDSLInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        accumDSLInfo.bindingCount = std::size(accumBindings);
        accumDSLInfo.pBindings = accumBindings;
        vkd.CreateDescriptorSetLayout(dev, &accumDSLInfo, nullptr, &m_accumDSL);

        // Resolve DSL
        VkDescriptorSetLayoutBinding resolveBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // active flag
        };
        VkDescriptorSetLayoutCreateInfo resolveDSLInfo = {};
        resolveDSLInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        resolveDSLInfo.bindingCount = std::size(resolveBindings);
        resolveDSLInfo.pBindings = resolveBindings;
        vkd.CreateDescriptorSetLayout(dev, &resolveDSLInfo, nullptr, &m_resolveDSL);

        // Pipeline layouts
        VkPushConstantRange pcRange = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FrameAnalyzer::PushConstants) };

        VkPipelineLayoutCreateInfo plInfo = {};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_accumDSL;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        vkd.CreatePipelineLayout(dev, &plInfo, nullptr, &m_accumLayout);

        plInfo.pSetLayouts = &m_resolveDSL;
        vkd.CreatePipelineLayout(dev, &plInfo, nullptr, &m_resolveLayout);

        // Shader modules
        auto createModule = [&](const std::vector<uint32_t>& code, VkShaderModule& mod) {
            VkShaderModuleCreateInfo smInfo = {};
            smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smInfo.codeSize = code.size() * sizeof(uint32_t);
            smInfo.pCode = code.data();
            vkd.CreateShaderModule(dev, &smInfo, nullptr, &mod);
        };

        createModule(decompressShaderCached(frame_accumulate_comp), m_accumShader);
        createModule(decompressShaderCached(frame_resolve_comp), m_resolveShader);

        // Pipelines
        struct SpecData { int32_t colorSpaceMode; };
        SpecData specData = { m_colorSpaceMode };
        VkSpecializationMapEntry specMapEntry = { 65535, 0, sizeof(int32_t) };
        VkSpecializationInfo specInfo = {};
        specInfo.mapEntryCount = 1;
        specInfo.pMapEntries = &specMapEntry;
        specInfo.dataSize = sizeof(SpecData);
        specInfo.pData = &specData;

        auto createPipeline = [&](VkShaderModule mod, VkPipelineLayout layout, VkPipeline& pipe) {
            VkComputePipelineCreateInfo cpInfo = {};
            cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module = mod;
            cpInfo.stage.pName = "main";
            cpInfo.stage.pSpecializationInfo = &specInfo;
            cpInfo.layout = layout;
            vkd.CreateComputePipelines(dev, m_pDevice->pipelineCache, 1, &cpInfo, nullptr, &pipe);
        };
        createPipeline(m_accumShader,   m_accumLayout,   m_accumPipeline);
        createPipeline(m_resolveShader, m_resolveLayout, m_resolvePipeline);

        // Descriptor pool
        uint32_t imgCount = (uint32_t)m_inputImages.size();

        uint32_t totalSamplers = 0, totalSSBOs = 0, totalStorageImages = 0;
        for (const auto& b : accumBindings) {
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) totalSamplers += imgCount;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)         totalSSBOs += imgCount;
        }
        for (const auto& b : resolveBindings) {
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) totalSSBOs += 1;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)  totalStorageImages += 1;
        }

        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalSamplers },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         totalSSBOs },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          totalStorageImages },
        };
        VkDescriptorPoolCreateInfo dpInfo = {};
        dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpInfo.maxSets = imgCount + 1; // accumulate per image + 1 resolve
        dpInfo.poolSizeCount = std::size(poolSizes);
        dpInfo.pPoolSizes = poolSizes;
        vkd.CreateDescriptorPool(dev, &dpInfo, nullptr, &m_pool);

        // Allocate descriptor sets accumulate one per image
        m_accumSets.resize(imgCount);
        {
            std::vector<VkDescriptorSetLayout> layouts(imgCount, m_accumDSL);
            VkDescriptorSetAllocateInfo ai = {};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_pool;
            ai.descriptorSetCount = imgCount;
            ai.pSetLayouts = layouts.data();
            vkd.AllocateDescriptorSets(dev, &ai, m_accumSets.data());
        }

        // Resolve: one set
        {
            VkDescriptorSetAllocateInfo ai = {};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &m_resolveDSL;
            vkd.AllocateDescriptorSets(dev, &ai, &m_resolveSet);
        }

        // Shared buffer infos for descriptor writes
        VkDescriptorBufferInfo histBuf   = { m_histBuffer,   0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo waveBuf   = { m_waveBuffer,   0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo vecBuf    = { m_vecBuffer,    0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo activeBuf = { m_activeBuffer, 0, VK_WHOLE_SIZE };

        // Write accumulate descriptors
        for (uint32_t i = 0; i < imgCount; i++)
        {
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.sampler = m_sampler;
            imgInfo.imageView = m_inputViews[i];
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[] = {
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfo },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &histBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &waveBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumSets[i], 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &vecBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumSets[i], 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &activeBuf },
            };
            vkd.UpdateDescriptorSets(dev, std::size(writes), writes, 0, nullptr);
        }

        // Write resolve descriptors
        {
            VkDescriptorImageInfo histImg = { VK_NULL_HANDLE, m_scopeViews[HISTOGRAM],   VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo waveImg = { VK_NULL_HANDLE, m_scopeViews[WAVEFORM],    VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo vecImg  = { VK_NULL_HANDLE, m_scopeViews[VECTORSCOPE], VK_IMAGE_LAYOUT_GENERAL };

            VkWriteDescriptorSet writes[] = {
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &histBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &waveBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &vecBuf },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &histImg },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &waveImg },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &vecImg },
                { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_resolveSet, 6, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &activeBuf },
            };
            vkd.UpdateDescriptorSets(dev, std::size(writes), writes, 0, nullptr);
        }

        Logger::debug("FrameAnalyzer resources created");
    }

    void FrameAnalyzer::recordCommands(VkCommandBuffer cmdBuf, uint32_t imageIndex)
    {
        if (imageIndex >= m_accumSets.size()) return;

        auto& vkd = m_pDevice->vkd;

        // First frame: transition output images UNDEFINED -> GENERAL
        if (!m_layoutsInitialized)
        {
            for (int i = 0; i < SCOPE_COUNT; i++)
            {
                VkImageMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = m_scopeImages[i];
                barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            m_layoutsInitialized = true;
        }

        m_pushConstants.enabled = 1;

        // 1. Clear SSBOs
        vkd.CmdFillBuffer(cmdBuf, m_histBuffer, 0, VK_WHOLE_SIZE, 0);
        vkd.CmdFillBuffer(cmdBuf, m_waveBuffer, 0, VK_WHOLE_SIZE, 0);
        vkd.CmdFillBuffer(cmdBuf, m_vecBuffer,  0, VK_WHOLE_SIZE, 0);

        // Barrier: TRANSFER_WRITE -> SHADER_RW
        {
            VkMemoryBarrier memBarrier = {};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }

        // 2. Accumulate pass
        vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumPipeline);
        vkd.CmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_accumLayout, 0, 1, &m_accumSets[imageIndex], 0, nullptr);
        vkd.CmdPushConstants(cmdBuf, m_accumLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(FrameAnalyzer::PushConstants), &m_pushConstants);
        vkd.CmdDispatch(cmdBuf, (m_extent.width + 15) / 16, (m_extent.height + 15) / 16, 1);

        // Barrier: SHADER_WRITE -> SHADER_RW (SSBO accumulate -> SSBO resolve read + image write)
        {
            VkMemoryBarrier memBarrier = {};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }

        // 3. Resolve pass
        vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolvePipeline);
        vkd.CmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_resolveLayout, 0, 1, &m_resolveSet, 0, nullptr);
        vkd.CmdPushConstants(cmdBuf, m_resolveLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(FrameAnalyzer::PushConstants), &m_pushConstants);
        vkd.CmdDispatch(cmdBuf, (SCOPE_DIM + 15) / 16, (SCOPE_DIM + 15) / 16, 1);

        // 4. Synchronization Barrier: Compute Write -> Fragment Read (ImGui) Without this scopes turns into sliding puzzles due to race conditions.
        {
            VkImageMemoryBarrier barriers[SCOPE_COUNT] = {};
            for (int i = 0; i < SCOPE_COUNT; i++) {
                barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].image = m_scopeImages[i];
                barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            }
            vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, SCOPE_COUNT, barriers);
        }
    }

    void FrameAnalyzer::destroyResources()
    {
        auto& vkd = m_pDevice->vkd;
        auto dev  = m_pDevice->device;

        if (m_mappedActive)     vkd.UnmapMemory(dev, m_activeMemory);
        if (m_activeBuffer)     vkd.DestroyBuffer(dev, m_activeBuffer, nullptr);
        if (m_activeMemory)     vkd.FreeMemory(dev, m_activeMemory, nullptr);
        if (m_pool)             vkd.DestroyDescriptorPool(dev, m_pool, nullptr);
        if (m_sampler)          vkd.DestroySampler(dev, m_sampler, nullptr);
        if (m_accumPipeline)    vkd.DestroyPipeline(dev, m_accumPipeline, nullptr);
        if (m_resolvePipeline)  vkd.DestroyPipeline(dev, m_resolvePipeline, nullptr);
        if (m_accumLayout)      vkd.DestroyPipelineLayout(dev, m_accumLayout, nullptr);
        if (m_resolveLayout)    vkd.DestroyPipelineLayout(dev, m_resolveLayout, nullptr);
        if (m_accumDSL)         vkd.DestroyDescriptorSetLayout(dev, m_accumDSL, nullptr);
        if (m_resolveDSL)       vkd.DestroyDescriptorSetLayout(dev, m_resolveDSL, nullptr);
        if (m_accumShader)      vkd.DestroyShaderModule(dev, m_accumShader, nullptr);
        if (m_resolveShader)    vkd.DestroyShaderModule(dev, m_resolveShader, nullptr);

        for (auto v : m_inputViews) if (v) vkd.DestroyImageView(dev, v, nullptr);
        for (int i = 0; i < SCOPE_COUNT; i++) {
            if (m_scopeViews[i])   vkd.DestroyImageView(dev, m_scopeViews[i], nullptr);
            if (m_scopeImages[i])  vkd.DestroyImage(dev, m_scopeImages[i], nullptr);
            if (m_scopeMemory[i])  vkd.FreeMemory(dev, m_scopeMemory[i], nullptr);
        }
        if (m_histBuffer) vkd.DestroyBuffer(dev, m_histBuffer, nullptr);
        if (m_waveBuffer) vkd.DestroyBuffer(dev, m_waveBuffer, nullptr);
        if (m_vecBuffer)  vkd.DestroyBuffer(dev, m_vecBuffer, nullptr);
        if (m_histMemory) vkd.FreeMemory(dev, m_histMemory, nullptr);
        if (m_waveMemory) vkd.FreeMemory(dev, m_waveMemory, nullptr);
        if (m_vecMemory)  vkd.FreeMemory(dev, m_vecMemory, nullptr);
    }
} // namespace vkBasalt
