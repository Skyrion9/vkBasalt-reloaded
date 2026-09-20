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
        Logger::err("AutoHdrAnalyzer: No suitable memory type found for requested properties");
        return 0;
    }

    VkBuffer AutoHdrAnalyzer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory& memory, VkMemoryPropertyFlags memProps) {
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer = nullptr;
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
            .width=extent.width, .height=extent.height,
            .invWidth=1.0f / static_cast<float>(extent.width),
            .invHeight=1.0f / static_cast<float>(extent.height),
            .sourceColorSpace=sourceColorSpace
        };
        m_accSpecMapEntries = {
            {.constantID=0, .offset=offsetof(AccumulateSpecData, width), .size=sizeof(uint32_t)},
            {.constantID=1, .offset=offsetof(AccumulateSpecData, height), .size=sizeof(uint32_t)},
            {.constantID=2, .offset=offsetof(AccumulateSpecData, invWidth), .size=sizeof(float)},
            {.constantID=3, .offset=offsetof(AccumulateSpecData, invHeight), .size=sizeof(float)},
            {.constantID=65535, .offset=offsetof(AccumulateSpecData, sourceColorSpace), .size=sizeof(int32_t)}
        };
        m_accSpecInfo = {
            .mapEntryCount=static_cast<uint32_t>(m_accSpecMapEntries.size()),
            .pMapEntries=m_accSpecMapEntries.data(),
            .dataSize=sizeof(AccumulateSpecData),
            .pData=&m_accSpecData
        };

        // Fetch system detected display values to use as intelligent fallbacks
        DisplayHdrInfo detected = detectDisplayHdrCalibration(pConfig, monitorName);
        float fallbackWhite = (detected.detected && detected.sdrWhitePointNits > 0.0f) ? detected.sdrWhitePointNits : 203.0f;
        float fallbackPeak = (detected.detected && detected.peakBrightnessNits > 0.0f) ? detected.peakBrightnessNits : 1000.0f;

        m_redSpecData = {
            .adaptationSpeed=std::clamp(pConfig->getOption<float>("hdrAdaptiveSpeed", 0.1f), 0.01f, 2.0f),
            .targetWhite=pConfig->getOption<float>("sdrWhitePointNits", fallbackWhite) * 0.01f,
            .targetPeak=pConfig->getOption<float>("hdrPeakNits", fallbackPeak) * 0.01f,
            .peakScale=std::clamp(pConfig->getOption<float>("hdrAdaptivePeakScale", 1.0f), 0.0f, 1.0f),
            .midtoneRange=std::clamp(pConfig->getOption<float>("hdrAdaptiveMidtoneRange", 0.05f), 0.0f, 0.2f),
            .calibrationMode=calibrationMode
        };
        m_redSpecMapEntries = {
            {.constantID=10, .offset=offsetof(ReduceSpecData, adaptationSpeed), .size=sizeof(float)},
            {.constantID=11, .offset=offsetof(ReduceSpecData, targetWhite), .size=sizeof(float)},
            {.constantID=12, .offset=offsetof(ReduceSpecData, targetPeak), .size=sizeof(float)},
            {.constantID=13, .offset=offsetof(ReduceSpecData, peakScale), .size=sizeof(float)},
            {.constantID=14, .offset=offsetof(ReduceSpecData, midtoneRange), .size=sizeof(float)},
            {.constantID=15, .offset=offsetof(ReduceSpecData, calibrationMode), .size=sizeof(int32_t)}
        };
        m_redSpecInfo = {
            .mapEntryCount=static_cast<uint32_t>(m_redSpecMapEntries.size()),
            .pMapEntries=m_redSpecMapEntries.data(),
            .dataSize=sizeof(ReduceSpecData),
            .pData=&m_redSpecData
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
        VkSamplerCreateInfo samplerInfo = {.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pLogicalDevice->vkd.CreateSampler(pLogicalDevice->device, &samplerInfo, nullptr, &m_sampler);

        // 4. Create Descriptor Set Layouts
        std::vector<VkDescriptorSetLayoutBinding> accBindings(2);
        accBindings[0] = {.binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers=nullptr};
        accBindings[1] = {.binding=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers=nullptr};
        VkDescriptorSetLayoutCreateInfo accLayoutInfo = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext=nullptr, .flags=0, .bindingCount=2, .pBindings=accBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &accLayoutInfo, nullptr, &m_accumulateSetLayout);

        std::vector<VkDescriptorSetLayoutBinding> redBindings(3);
        redBindings[0] = {.binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers=nullptr};
        redBindings[1] = {.binding=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers=nullptr};
        redBindings[2] = {.binding=2, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers=nullptr};
        VkDescriptorSetLayoutCreateInfo redLayoutInfo = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext=nullptr, .flags=0, .bindingCount=3, .pBindings=redBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &redLayoutInfo, nullptr, &m_reduceSetLayout);

        std::vector<VkDescriptorSetLayoutBinding> metBindings(1);
        metBindings[0] = {.binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers=nullptr};
        VkDescriptorSetLayoutCreateInfo metLayoutInfo = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext=nullptr, .flags=0, .bindingCount=1, .pBindings=metBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &metLayoutInfo, nullptr, &m_metricsSetLayout);

        // 5. Descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount=m_imageCount},
            {.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=m_imageCount * 5}
        };
        VkDescriptorPoolCreateInfo poolInfo = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext=nullptr, .flags=0, .maxSets=m_imageCount * 3, .poolSizeCount=static_cast<uint32_t>(poolSizes.size()), .pPoolSizes=poolSizes.data()};
        pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &poolInfo, nullptr, &m_descriptorPool);

        // 6. Allocate Descriptor Sets
        m_accumulateSets.resize(m_imageCount);
        m_reduceSets.resize(m_imageCount);
        m_metricsDescriptorSets.resize(m_imageCount);

        std::vector<VkDescriptorSetLayout> accLayouts(m_imageCount, m_accumulateSetLayout);
        std::vector<VkDescriptorSetLayout> redLayouts(m_imageCount, m_reduceSetLayout);
        std::vector<VkDescriptorSetLayout> metLayouts(m_imageCount, m_metricsSetLayout);

        VkDescriptorSetAllocateInfo allocInfo = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext=nullptr, .descriptorPool=m_descriptorPool, .descriptorSetCount=m_imageCount, .pSetLayouts=accLayouts.data()};
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_accumulateSets.data());
        allocInfo.pSetLayouts = redLayouts.data();
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_reduceSets.data());
        allocInfo.pSetLayouts = metLayouts.data();
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocInfo, m_metricsDescriptorSets.data());

        // 7. Write Static Descriptors
        VkDescriptorBufferInfo histBufInfo = {.buffer=m_histogramBuffer, .offset=0, .range=VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tempBufInfo = {.buffer=m_temporalBuffer, .offset=0, .range=VK_WHOLE_SIZE};
        VkDescriptorBufferInfo metBufInfo = {.buffer=m_metricsBuffer, .offset=0, .range=VK_WHOLE_SIZE};

        for (uint32_t i = 0; i < m_imageCount; i++) {
            VkWriteDescriptorSet redWrites[3] = {};
            redWrites[0] = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_reduceSets[i], .dstBinding=0, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo=nullptr, .pBufferInfo=&histBufInfo, .pTexelBufferView=nullptr};
            redWrites[1] = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_reduceSets[i], .dstBinding=1, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo=nullptr, .pBufferInfo=&tempBufInfo, .pTexelBufferView=nullptr};
            redWrites[2] = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_reduceSets[i], .dstBinding=2, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo=nullptr, .pBufferInfo=&metBufInfo, .pTexelBufferView=nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 3, redWrites, 0, nullptr);

            VkWriteDescriptorSet metWrite = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_metricsDescriptorSets[i], .dstBinding=0, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo=nullptr, .pBufferInfo=&metBufInfo, .pTexelBufferView=nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &metWrite, 0, nullptr);
            
            VkWriteDescriptorSet accWrite = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_accumulateSets[i], .dstBinding=1, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo=nullptr, .pBufferInfo=&histBufInfo, .pTexelBufferView=nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &accWrite, 0, nullptr);
        }

        // 8. Shader Modules
        VkShaderModuleCreateInfo smInfo = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        const auto& accumulateSpirv = decompressShaderCached(auto_hdr_accumulate_comp);
        smInfo.codeSize = accumulateSpirv.size() * sizeof(uint32_t);
        smInfo.pCode = accumulateSpirv.data();
        pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &smInfo, nullptr, &m_accumulateModule);

        const auto& reduceSpirv = decompressShaderCached(auto_hdr_reduce_comp);
        smInfo.codeSize = reduceSpirv.size() * sizeof(uint32_t);
        smInfo.pCode = reduceSpirv.data();
        pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &smInfo, nullptr, &m_reduceModule);

        // 9. Pipeline Layouts & Pipelines
        VkPipelineLayoutCreateInfo accPlInfo = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pNext=nullptr, .flags=0, .setLayoutCount=1, .pSetLayouts=&m_accumulateSetLayout, .pushConstantRangeCount=0, .pPushConstantRanges=nullptr};
        pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &accPlInfo, nullptr, &m_accumulateLayout);

        VkComputePipelineCreateInfo accCpInfo = {.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        accCpInfo.stage = {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext=nullptr, .flags=0, .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=m_accumulateModule, .pName="main", .pSpecializationInfo=nullptr};
        accCpInfo.stage.pSpecializationInfo = &m_accSpecInfo;
        accCpInfo.layout = m_accumulateLayout;
        pLogicalDevice->vkd.CreateComputePipelines(pLogicalDevice->device, pLogicalDevice->pipelineCache, 1, &accCpInfo, nullptr, &m_accumulatePipeline);

        // Reduce: Only deltaTime (4 bytes) as push constant
        VkPushConstantRange redPushRange = {.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .offset=0, .size=4}; 
        VkPipelineLayoutCreateInfo redPlInfo = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pNext=nullptr, .flags=0, .setLayoutCount=1, .pSetLayouts=&m_reduceSetLayout, .pushConstantRangeCount=1, .pPushConstantRanges=&redPushRange};
        pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &redPlInfo, nullptr, &m_reduceLayout);

        VkComputePipelineCreateInfo redCpInfo = {.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        redCpInfo.stage = {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext=nullptr, .flags=0, .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=m_reduceModule, .pName="main", .pSpecializationInfo=nullptr};
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
        const auto* data = static_cast<const float*>(m_mappedMetrics);
        outWhite     = data[0] * 100.0f; // nits * 0.01 -> nits
        outPeak      = data[1] * 100.0f;
        outIntensity = data[2];
    }

    bool AutoHdrAnalyzer::getUpdatedMetadata(float& outPeak, float& outWhite) {
        if (!m_mappedMetrics) return false;
        const auto* data = static_cast<const float*>(m_mappedMetrics);
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
            VkDescriptorImageInfo imgInfo = {.sampler=m_sampler, .imageView=inputImageViews[i], .imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet accWrite = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext=nullptr, .dstSet=m_accumulateSets[i], .dstBinding=0, .dstArrayElement=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo=&imgInfo, .pBufferInfo=nullptr, .pTexelBufferView=nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &accWrite, 0, nullptr);
        }
    }

    void AutoHdrAnalyzer::recordCommands(VkCommandBuffer cmdBuf, VkImageView  /*inputImageView*/, uint32_t imageIndex) {
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

        VkBufferCopy copyRegion = {.srcOffset=0, .dstOffset=0, .size=16};
        pLogicalDevice->vkd.CmdCopyBuffer(cmdBuf, m_metricsBuffer, m_stagingMetricsBuffer, 1, &copyRegion);
    }
} // namespace vkBasalt
