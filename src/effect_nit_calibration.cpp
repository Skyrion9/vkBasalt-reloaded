#include "effect_nit_calibration.hpp"
#include "shader_sources.hpp"
#include "logger.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>

namespace vkBasalt 
{
    #define SPEC(id, field) .specId = id, .specOffset = offsetof(NitCalibrationSpecData, field), .specSize = sizeof(((NitCalibrationSpecData*)0)->field)

    // Static accessor, callable without an instance (used by AutoHDR tab when effect isn't in chain)
    const std::vector<EffectParamDesc>& NitCalibrationEffect::getCalibrationParams()
    {
        static const std::vector<EffectParamDesc> params = {
            {.key = "hdrToneMapper", .label = "Tone Mapper", .type = ParamType::Combo,
             .defaultVal = 2.0, .minVal = 0.0, .maxVal = 2.0, .step = 1.0,
             .comboOptions = {"quality", "fast", "hermite"},
             .category = "Display Calibration",
             .tooltip = "HDR tone mapping algorithm.\n"
                        "quality: Reinhard, rational curve with matched slope, Hunt effect, achromatic clipping.\n"
                        "fast: Polynomial sRGB decode, simpler curve, MaxRGB clamp.\n"
                        "hermite: BT.2390 cubic spline, C1 continuous roll-off (default).",
             SPEC(65533, toneMapperMode)},

            {.key = "sdrWhitePointNits", .label = "SDR White Point (nits)", .type = ParamType::Float,
             .defaultVal = 203.0, .minVal = 80.0, .maxVal = 400.0, .step = 1.0,
             .category = "Display Calibration",
             .tooltip = "Reference white luminance for SDR content.\n"
                        "100 = ITU-R BT.709 reference\n"
                        "203 = HDR10 standard SDR white\n"
                        "80-120 = dim room viewing",
             SPEC(0, sdrWhitePoint)},

            {.key = "hdrPeakNits", .label = "Peak Brightness (nits)", .type = ParamType::Float,
             .defaultVal = 1000.0, .minVal = 200.0, .maxVal = 4000.0, .step = 10.0,
             .category = "Display Calibration",
             .tooltip = "Maximum display luminance.\n"
                        "Clamps HDR highlights to your display's measured peak.\n"
                        "Common values: 400 (entry HDR), 600-1000 (mid-range), 1000-2000 (high-end OLED/MiniLED).",
             SPEC(1, hdrPeakNits)},
        };
        return params;
    }

    NitCalibrationEffect::NitCalibrationEffect(LogicalDevice* pLogicalDevice, 
                                               VkFormat sourceFormat, VkFormat destFormat, VkExtent2D imageExtent,
                                               std::vector<VkImage> inputImages, std::vector<VkImage> outputImages,
                                               Config* pConfig, 
                                               VkColorSpaceKHR sourceColorSpace, VkColorSpaceKHR destColorSpace,
                                               bool autoHdrActive) {
        Logger::debug("Creating HDR Output Effect");
        vertexCode = full_screen_triangle_vert;
        fragmentCode = nit_calibration_frag;
        
        m_pConfigRef = pConfig;
        m_autoHdrActive = autoHdrActive;
        
        // Read config, apply defaults, clamp, write to specData by offset, and build mapEntries
        NitCalibrationSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(getParamDescs().size() + 3); // +3 for autoHdr, source/dest colorspace

        const auto& params = getParamDescs();
        for (const auto& p : params) {
            if (p.specId < 0) continue;
            
            double def = p.defaultVal;
            double val;
            
            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = static_cast<int>(p.defaultVal);
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) {
                        idx = static_cast<int>(ci);
                        break;
                    }
                }
                val = static_cast<double>(idx);
            } else if (p.type == ParamType::Float) {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(def)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(def)));
            }
            
            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;
            
            if (p.type == ParamType::Float) {
                float f = (float)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &f, sizeof(float));
            } else {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &i, sizeof(int32_t));
            }
            
            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        // Add non-user-configurable specialization constants
        specData.autoHdrEnabled = autoHdrActive ? 1 : 0;
        mapEntries.push_back({2, offsetof(NitCalibrationSpecData, autoHdrEnabled), sizeof(int32_t)});
        
        specData.sourceColorSpace = static_cast<int32_t>(getColorSpaceMode(sourceFormat, sourceColorSpace));
        mapEntries.push_back({65534, offsetof(NitCalibrationSpecData, sourceColorSpace), sizeof(int32_t)});
        
        specData.destColorSpace = static_cast<int32_t>(getColorSpaceMode(destFormat, destColorSpace));
        mapEntries.push_back({65535, offsetof(NitCalibrationSpecData, destColorSpace), sizeof(int32_t)});
        
        // Determine if adaptive analyzer should be created before setting spec data
        m_hdrAdaptive = pConfig->getOption<bool>("hdrAdaptive", true);
        bool willCreateAnalyzer = m_hdrAdaptive && (autoHdrActive || getColorSpaceMode(sourceFormat, sourceColorSpace) != ColorSpaceMode::SDR_SRGB);
        
        m_specData = specData;
        m_specMapEntries = mapEntries;
        
        m_specInfo.mapEntryCount = (uint32_t)m_specMapEntries.size();
        m_specInfo.pMapEntries = m_specMapEntries.data();
        m_specInfo.dataSize = sizeof(NitCalibrationSpecData);
        m_specInfo.pData = &m_specData;
        
        pVertexSpecInfo = nullptr;
        pFragmentSpecInfo = &m_specInfo;
        
    // Create analyzer or dummy metrics buffer and add its descriptor set layout before init()
    if (willCreateAnalyzer) {
        int32_t srcCsmInt = static_cast<int32_t>(getColorSpaceMode(sourceFormat, sourceColorSpace));
        m_autoHdrAnalyzer = std::make_unique<AutoHdrAnalyzer>(pLogicalDevice, imageExtent, inputImages.size(), pConfig, srcCsmInt);
        this->descriptorSetLayouts.push_back(m_autoHdrAnalyzer->getMetricsSetLayout());
    } else {
        // Create dummy metrics set layout
        std::vector<VkDescriptorSetLayoutBinding> metBindings(1);
        metBindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo metLayoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, metBindings.data()};
        pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &metLayoutInfo, nullptr, &m_dummyMetricsSetLayout);
        this->descriptorSetLayouts.push_back(m_dummyMetricsSetLayout);

        // Create dummy metrics buffer (DEVICE_LOCAL for optimal GPU cache behavior)
        VkBufferCreateInfo bufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = 16;
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &bufInfo, nullptr, &m_dummyMetricsBuffer);
        VkMemoryRequirements memReqs;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, m_dummyMetricsBuffer, &memReqs);
        VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        
        auto findMemType = [&](uint32_t typeBits, VkMemoryPropertyFlags props) -> uint32_t {
            for (uint32_t i = 0; i < pLogicalDevice->memoryProperties.memoryTypeCount; i++) {
                if ((typeBits & (1 << i)) && (pLogicalDevice->memoryProperties.memoryTypes[i].propertyFlags & props) == props) return i;
            }
            return 0;
        };
        
        allocInfo.memoryTypeIndex = findMemType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, &allocInfo, nullptr, &m_dummyMetricsMemory);
        pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, m_dummyMetricsBuffer, m_dummyMetricsMemory, 0);
        
        // Store static values to upload via CmdUpdateBuffer on the first frame
        m_dummyMetricsData[0] = pConfig->getOption<float>("sdrWhitePointNits", 203.0f) * 0.01f;
        m_dummyMetricsData[1] = pConfig->getOption<float>("hdrPeakNits", 1000.0f) * 0.01f;
        m_dummyMetricsData[2] = 1.0f;
        m_dummyMetricsData[3] = 0.0f;

        // Create descriptor pool and sets
        VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)inputImages.size()};
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, (uint32_t)inputImages.size(), 1, &poolSize};
        pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &poolInfo, nullptr, &m_dummyMetricsPool);

        m_dummyMetricsSets.resize(inputImages.size());
        std::vector<VkDescriptorSetLayout> layouts(inputImages.size(), m_dummyMetricsSetLayout);
        VkDescriptorSetAllocateInfo allocSetInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_dummyMetricsPool, (uint32_t)inputImages.size(), layouts.data()};
        pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &allocSetInfo, m_dummyMetricsSets.data());

        VkDescriptorBufferInfo bufInfoDesc = {m_dummyMetricsBuffer, 0, VK_WHOLE_SIZE};
        for (size_t i = 0; i < inputImages.size(); i++) {
            VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_dummyMetricsSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufInfoDesc, nullptr};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &write, 0, nullptr);
        }
    }
        
        // Init with destFormat so renderpass/framebuffers match the real HDR swapchain
        init(pLogicalDevice, destFormat, imageExtent, inputImages, outputImages, pConfig);

        // Recreate input views with sourceFormat as fake images are SDR
        if (sourceFormat != destFormat) {
            for (size_t i = 0; i < inputImages.size(); i++) {
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
                VkImageViewCreateInfo viewInfo = {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = inputImages[i];
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = sourceFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &viewInfo, nullptr, &inputImageViews[i]);

                VkDescriptorImageInfo imgInfo = {};
                imgInfo.sampler = sampler;
                imgInfo.imageView = inputImageViews[i];
                imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = imageDescriptorSets[i];
                write.dstBinding = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imgInfo;
                pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &write, 0, nullptr);
            }
        }
        // Bind the static input views to the analyzers descriptor sets only once
        if (m_autoHdrAnalyzer) {
            m_autoHdrAnalyzer->updateInputViews(inputImageViews);
        }
    }

    NitCalibrationEffect::~NitCalibrationEffect() {
        if (m_dummyMetricsBuffer) pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_dummyMetricsBuffer, nullptr);
        if (m_dummyMetricsMemory) pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_dummyMetricsMemory, nullptr);
        if (m_dummyMetricsSetLayout) pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, m_dummyMetricsSetLayout, nullptr);
        if (m_dummyMetricsPool) pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, m_dummyMetricsPool, nullptr);
    }

    void NitCalibrationEffect::updateEffect() {}

    void NitCalibrationEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) {
        // Barrier 1: Acquire inputImages for reading (Compute + Fragment)
        VkImageMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        // Initialize DEVICE_LOCAL dummy metrics buffer on first frame
        if (!m_autoHdrAnalyzer && !m_dummyMetricsInitialized) {
            pLogicalDevice->vkd.CmdUpdateBuffer(commandBuffer, m_dummyMetricsBuffer, 0, 16, m_dummyMetricsData);
            
            VkBufferMemoryBarrier initBarrier = {};
            initBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.buffer = m_dummyMetricsBuffer;
            initBarrier.offset = 0;
            initBarrier.size = 16;
            pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &initBarrier, 0, nullptr);
            
            m_dummyMetricsInitialized = true;
        }

        // Run AutoHDR Analyzer (Compute passes)
        if (m_autoHdrAnalyzer) {
            m_autoHdrAnalyzer->recordCommands(commandBuffer, inputImageViews[imageIndex], imageIndex);
            
            // Barrier: Compute SSBO write -> Fragment SSBO read
            VkMemoryBarrier memBarrier = {};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }

        // Render Pass
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Bind Set 0 (Image + UBO)
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
            
        // Bind Set 1 (Metrics SSBO)
        if (m_autoHdrAnalyzer) {
            VkDescriptorSet metricSet = m_autoHdrAnalyzer->getMetricsDescriptorSet(imageIndex);
            pLogicalDevice->vkd.CmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &metricSet, 0, nullptr);
        } else {
            pLogicalDevice->vkd.CmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &m_dummyMetricsSets[imageIndex], 0, nullptr);
        }
        
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier secondBarrier = {};
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &secondBarrier);

        // Barrier 3: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier thirdBarrier = {};
            thirdBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            thirdBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Left by render pass
            thirdBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            thirdBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.image               = outputImages[imageIndex];
            thirdBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &thirdBarrier);
        }
    }

} // namespace vkBasalt
