#include "auto_hdr_analyzer.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>

#include "shader_sources.hpp"
#include "logger.hpp"
#include "hdr_detect.hpp"


namespace vkBasalt {

    uint32_t AutoHdrAnalyzer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) {
        for (uint32_t i = 0; i < pLogicalDevice->memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & (1 << i)) && (pLogicalDevice->memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

    VkBuffer AutoHdrAnalyzer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory& memory, VkMemoryPropertyFlags memProps) {
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer;
        if (pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &info, nullptr, &buffer) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        VkMemoryRequirements memReqs;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memReqs);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, memProps);
        if (pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            return VK_NULL_HANDLE;
        }
        pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, memory, 0);
        return buffer;
    }

    AutoHdrAnalyzer::AutoHdrAnalyzer(LogicalDevice* pDevice, VkExtent2D extent, uint32_t imageCount, Config* pConfig, int32_t sourceColorSpace, int32_t calibrationMode, const std::string& monitorName)
        : pLogicalDevice(pDevice), m_extent(extent), m_imageCount(imageCount) {
        
        m_lastTime = std::chrono::steady_clock::now();

        // 1. Populate Specialization Data
        m_accSpecData = {
            extent.width, extent.height,
            1.0f / static_cast<float>(extent.width),
            1.0f / static_cast<float>(extent.height),
            sourceColorSpace
        };
        m_accSpecMapEntries = {
            {0, offsetof(AccumulateSpecData, width), sizeof(uint32_t)},
            {1, offsetof(AccumulateSpecData, height), sizeof(uint32_t)},
            {2, offsetof(AccumulateSpecData, invWidth), sizeof(float)},
            {3, offsetof(AccumulateSpecData, invHeight), sizeof(float)},
            {65535, offsetof(AccumulateSpecData, sourceColorSpace), sizeof(int32_t)}
        };
        m_accSpecInfo = {
            (uint32_t)m_accSpecMapEntries.size(),
            m_accSpecMapEntries.data(),
            sizeof(AccumulateSpecData),
            &m_accSpecData
        };

        // Fetch system detected display values to use as intelligent fallbacks
        DisplayHdrInfo detected = detectDisplayHdrCalibration(pConfig, monitorName);
        float fallbackWhite = (detected.detected && detected.sdrWhitePointNits > 0.0f) ? detected.sdrWhitePointNits : 203.0f;
        float fallbackPeak = (detected.detected && detected.peakBrightnessNits > 0.0f) ? detected.peakBrightnessNits : 1000.0f;

        m_redSpecData = {
            std::clamp(pConfig->getOption<float>("hdrAdaptiveSpeed", 0.1f), 0.01f, 2.0f),
            pConfig->getOption<float>("sdrWhitePointNits", fallbackWhite) * 0.01f,
            pConfig->getOption<float>("hdrPeakNits", fallbackPeak) * 0.01f,
            std::clamp(pConfig->getOption<float>("hdrAdaptivePeakScale", 1.0f), 0.0f, 1.0f),
            std::clamp(pConfig->getOption<float>("hdrAdaptiveMidtoneRange", 0.05f), 0.0f, 0.2f),
            calibrationMode
        };
        m_redSpecMapEntries = {
            {10, offsetof(ReduceSpecData, adaptationSpeed), sizeof(float)},
            {11, offsetof(ReduceSpecData, targetWhite), sizeof(float)},
            {12, offsetof(ReduceSpecData, targetPeak), sizeof(float)},
            {13, offsetof(ReduceSpecData, peakScale), sizeof(float)},
            {14, offsetof(ReduceSpecData, midtoneRange), sizeof(float)},
            {15, offsetof(ReduceSpecData, calibrationMode), sizeof(int32_t)}
        };
        m_redSpecInfo = {
            (uint32_t)m_redSpecMapEntries.size(),
            m_redSpecMapEntries.data(),
            sizeof(ReduceSpecData),
            &m_redSpecData
        };

        // 2. Create Buffers
        m_histogramBuffer = createBuffer(256 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_histogramMemory);
        // Temporal buffer must be DEVICE_LOCAL for fast GPU side feedback loop, we initialize it via CmdUpdateBuffer on the first frame.
        m_temporalBuffer = createBuffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_temporalMemory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_metricsBuffer = createBuffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_metricsMemory);
        
        // Staging buffer for CPU readback of dynamic HDR metadata
        m_stagingMetricsBuffer = createBuffer(16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_stagingMetricsMemory,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        pLogicalDevice->vkd.MapMemory(pLogicalDevice->device, m_stagingMetricsMemory, 0, VK_WHOLE_SIZE, 0, &m_mappedMetrics);
        
        // Zero init to prevent reading uninitialized VRAM garbage on the first frame before the GPU copy completes.
        if (m_mappedMetrics) {
            std::memset(m_mappedMetrics, 0, 16);
        }

        // 3. Create Sampler
        VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pLogicalDevice->vkd.CreateSampler(pLogicalDevice->device, &samplerInfo, nullptr, &m_sampler);

        // 4. Create Descriptor Set Layouts
        std::vector<VkDescriptorSetLayoutBinding> accBindings(2);
        accBindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        accBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo accLayoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, accBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &accLayoutInfo, nullptr, &m_accumulateSetLayout);

        std::vector<VkDescriptorSetLayoutBinding> redBindings(3);
        redBindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        redBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        redBindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo redLayoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, redBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &redLayoutInfo, nullptr, &m_reduceSetLayout);

        std::vector<VkDescriptorSetLayoutBinding> metBindings(1);
        metBindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo metLayoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, metBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &metLayoutInfo, nullptr, &m_metricsSetLayout);

        // 5. Descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_imageCount},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_imageCount * 5}
        };
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, m_imageCount * 3, (uint32_t)poolSizes.size(), poolSizes.data()};
        pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &poolInfo, nullptr, &m_descriptorPool);

        // 6. Allocate Descriptor Sets
        m_accumulateSets.resize(m_imageCount);
        m_reduceSets.resize(m_imageCount);
        m_metricsDescriptorSets.resize(m_imageCount);

        std::vector<VkDescriptorSetLayout> accLayouts(m_imageCount, m_accumulateSetLayout);
        std::vector<VkDescriptorSetLayout> redLayouts(m_imageCount, m_reduceSetLayout);
        std::vector<VkDescriptorSetLayout> metLayouts(m_imageCount, m_metricsSetLayout);

        VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_descriptorPool, m_imageCount, accLayouts.data()};
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_accumulateSets.data());
        allocInfo.pSetLayouts = redLayouts.data();
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_reduceSets.data());
        allocInfo.pSetLayouts = metLayouts.data();
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_metricsDescriptorSets.data());

        // 7. Write Static Descriptors
        VkDescriptorBufferInfo histBufInfo = {m_histogramBuffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tempBufInfo = {m_temporalBuffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo metBufInfo = {m_metricsBuffer, 0, VK_WHOLE_SIZE};

        for (uint32_t i = 0; i < m_imageCount; i++) {
            VkWriteDescriptorSet redWrites[3] = {};
            redWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_reduceSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &histBufInfo, nullptr};
            redWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_reduceSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tempBufInfo, nullptr};
            redWrites[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_reduceSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &metBufInfo, nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 3, redWrites, 0, nullptr);

            VkWriteDescriptorSet metWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_metricsDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &metBufInfo, nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &metWrite, 0, nullptr);
            
            VkWriteDescriptorSet accWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumulateSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &histBufInfo, nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &accWrite, 0, nullptr);
        }

        // 8. Shader Modules
        VkShaderModuleCreateInfo smInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smInfo.codeSize = auto_hdr_accumulate_comp.size() * sizeof(uint32_t);
        smInfo.pCode = auto_hdr_accumulate_comp.data();
        pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &smInfo, nullptr, &m_accumulateModule);

        smInfo.codeSize = auto_hdr_reduce_comp.size() * sizeof(uint32_t);
        smInfo.pCode = auto_hdr_reduce_comp.data();
        pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &smInfo, nullptr, &m_reduceModule);

        // 9. Pipeline Layouts & Pipelines
        VkPipelineLayoutCreateInfo accPlInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_accumulateSetLayout, 0, nullptr};
        pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &accPlInfo, nullptr, &m_accumulateLayout);

        VkComputePipelineCreateInfo accCpInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        accCpInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_accumulateModule, "main", nullptr};
        accCpInfo.stage.pSpecializationInfo = &m_accSpecInfo;
        accCpInfo.layout = m_accumulateLayout;
        pLogicalDevice->vkd.CreateComputePipelines(pLogicalDevice->device, pLogicalDevice->pipelineCache, 1, &accCpInfo, nullptr, &m_accumulatePipeline);

        // Reduce: Only deltaTime (4 bytes) as push constant
        VkPushConstantRange redPushRange = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4}; 
        VkPipelineLayoutCreateInfo redPlInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_reduceSetLayout, 1, &redPushRange};
        pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &redPlInfo, nullptr, &m_reduceLayout);

        VkComputePipelineCreateInfo redCpInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        redCpInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_reduceModule, "main", nullptr};
        redCpInfo.stage.pSpecializationInfo = &m_redSpecInfo;
        redCpInfo.layout = m_reduceLayout;
        pLogicalDevice->vkd.CreateComputePipelines(pLogicalDevice->device, pLogicalDevice->pipelineCache, 1, &redCpInfo, nullptr, &m_reducePipeline);

        Logger::debug("AutoHdrAnalyzer initialized");
    }

    void AutoHdrAnalyzer::getCurrentMetrics(float& outWhite, float& outPeak, float& outIntensity) const {
        if (!m_mappedMetrics) {
            outWhite = outPeak = outIntensity = 0.0f;
            return;
        }
        const float* data = static_cast<const float*>(m_mappedMetrics);
        outWhite     = data[0] * 100.0f; // nits * 0.01 -> nits
        outPeak      = data[1] * 100.0f;
        outIntensity = data[2];
    }

    bool AutoHdrAnalyzer::getUpdatedMetadata(float& outPeak, float& outWhite) {
        if (!m_mappedMetrics) return false;
        const float* data = static_cast<const float*>(m_mappedMetrics);
        // Shader stores nits * 0.01f, multiply by 100 to get actual nits
        float peak = data[1] * 100.0f; 
        float white = data[0] * 100.0f;
        
        // Only update if changed significantly (> 1 nit) to avoid spamming the driver
        if (std::abs(peak - m_lastPeak) > 1.0f || std::abs(white - m_lastWhite) > 1.0f) {
            outPeak = peak;
            outWhite = white;
            m_lastPeak = peak;
            m_lastWhite = white;
            return true;
        }
        return false;
    }

    AutoHdrAnalyzer::~AutoHdrAnalyzer() {
        if (!pLogicalDevice) return;
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, m_accumulatePipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, m_reducePipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, m_accumulateLayout, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, m_reduceLayout, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, m_accumulateModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, m_reduceModule, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, m_descriptorPool, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, m_accumulateSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, m_reduceSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, m_metricsSetLayout, nullptr);
        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, m_sampler, nullptr);
        if (m_histogramBuffer) pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_histogramBuffer, nullptr);
        if (m_temporalBuffer) pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_temporalBuffer, nullptr);
        if (m_mappedMetrics) pLogicalDevice->vkd.UnmapMemory(pLogicalDevice->device, m_stagingMetricsMemory);
        if (m_stagingMetricsBuffer) pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_stagingMetricsBuffer, nullptr);
        if (m_stagingMetricsMemory) pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_stagingMetricsMemory, nullptr);
        if (m_metricsBuffer) pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_metricsBuffer, nullptr);
        if (m_histogramMemory) pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_histogramMemory, nullptr);
        if (m_temporalMemory) pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_temporalMemory, nullptr);
        if (m_metricsMemory) pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_metricsMemory, nullptr);
    }

    void AutoHdrAnalyzer::updateInputViews(const std::vector<VkImageView>& inputImageViews) {
        for (uint32_t i = 0; i < m_imageCount; i++) {
            VkDescriptorImageInfo imgInfo = {m_sampler, inputImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet accWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_accumulateSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfo, nullptr, nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &accWrite, 0, nullptr);
        }
    }

    void AutoHdrAnalyzer::recordCommands(VkCommandBuffer cmdBuf, VkImageView inputImageView, uint32_t imageIndex) {
        // Initialize temporal buffer on first run using CmdUpdateBuffer (avoids PCIe staging buffer overhead)
        if (!m_temporalInitialized) {
            float temporalDefaults[4] = {0.5f, 0.18f, 0.0f, 0.0f}; // smoothedP99, smoothedAvg, padding
            pLogicalDevice->vkd.CmdUpdateBuffer(cmdBuf, m_temporalBuffer, 0, 16, temporalDefaults);
            
            VkBufferMemoryBarrier initBarrier = {};
            initBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.buffer = m_temporalBuffer;
            initBarrier.offset = 0;
            initBarrier.size = 16;
            pLogicalDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &initBarrier, 0, nullptr);
            
            m_temporalInitialized = true;
        }

        pLogicalDevice->vkd.CmdFillBuffer(cmdBuf, m_histogramBuffer, 0, VK_WHOLE_SIZE, 0);

        VkBufferMemoryBarrier histBarrier = {};
        histBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        histBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        histBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        histBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.buffer = m_histogramBuffer;
        histBarrier.offset = 0;
        histBarrier.size = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &histBarrier, 0, nullptr);

        // Accumulate Pass
        pLogicalDevice->vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumulatePipeline);
        pLogicalDevice->vkd.CmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumulateLayout, 0, 1, &m_accumulateSets[imageIndex], 0, nullptr);
        
        // opt: Half resolution dispatch for stride 2 sampling in the shader
        uint32_t groupCountX = ((m_extent.width / 2) + 15) / 16;
        uint32_t groupCountY = ((m_extent.height / 2) + 15) / 16;
        pLogicalDevice->vkd.CmdDispatch(cmdBuf, groupCountX, groupCountY, 1);

        VkBufferMemoryBarrier accToRedBarrier = {};
        accToRedBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        accToRedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        accToRedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        accToRedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        accToRedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        accToRedBarrier.buffer = m_histogramBuffer;
        accToRedBarrier.offset = 0;
        accToRedBarrier.size = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &accToRedBarrier, 0, nullptr);

        // Reduce Pass (Only deltaTime pushed)
        pLogicalDevice->vkd.CmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_reducePipeline);
        pLogicalDevice->vkd.CmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_reduceLayout, 0, 1, &m_reduceSets[imageIndex], 0, nullptr);
        
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - m_lastTime).count();
        m_lastTime = now;
        if (dt > 0.1f) dt = 0.1f;
        
        pLogicalDevice->vkd.CmdPushConstants(cmdBuf, m_reduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &dt);
        pLogicalDevice->vkd.CmdDispatch(cmdBuf, 1, 1, 1);

        // Copy metrics to staging buffer for CPU readback (dynamic HDR metadata)
        VkBufferMemoryBarrier reduceToCopyBarrier = {};
        reduceToCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        reduceToCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        reduceToCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        reduceToCopyBarrier.buffer = m_metricsBuffer;
        reduceToCopyBarrier.offset = 0;
        reduceToCopyBarrier.size = 16;
        pLogicalDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
            0, 0, nullptr, 1, &reduceToCopyBarrier, 0, nullptr);

        VkBufferCopy copyRegion = {0, 0, 16};
        pLogicalDevice->vkd.CmdCopyBuffer(cmdBuf, m_metricsBuffer, m_stagingMetricsBuffer, 1, &copyRegion);
    }
} // namespace vkBasalt
