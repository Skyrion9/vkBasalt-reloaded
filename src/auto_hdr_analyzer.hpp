#pragma once
#include "logical_device.hpp"
#include "config.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <chrono>

namespace vkBasalt
{

    class AutoHdrAnalyzer
    {
    public:
        AutoHdrAnalyzer(
            LogicalDevice* pDevice,
            VkExtent2D extent,
            uint32_t imageCount,
            Config* pConfig,
            int32_t sourceColorSpace,
            int32_t calibrationMode        = 0,
            const std::string& monitorName = "");
        ~AutoHdrAnalyzer();

        void recordCommands(VkCommandBuffer cmdBuf, VkImageView inputImageView, uint32_t imageIndex);
        void updateInputViews(const std::vector<VkImageView>& inputImageViews);
        bool getUpdatedMetadata(float& outPeak, float& outWhite);
        void getCurrentMetrics(float& outWhite, float& outPeak, float& outIntensity) const;

        [[nodiscard]] VkDescriptorSetLayout getMetricsSetLayout() const { return m_metricsSetLayout; }
        [[nodiscard]] VkDescriptorSet getMetricsDescriptorSet(uint32_t imageIndex) const
        {
            return m_metricsDescriptorSets[imageIndex];
        }

    private:
        LogicalDevice* pLogicalDevice;
        VkExtent2D m_extent;
        uint32_t m_imageCount;
        std::chrono::steady_clock::time_point m_lastTime;

        struct AccumulateSpecData
        {
            uint32_t width;
            uint32_t height;
            float invWidth;
            float invHeight;
            int32_t sourceColorSpace;
        };
        struct ReduceSpecData
        {
            float adaptationSpeed;
            float targetWhite;
            float targetPeak;
            float peakScale;
            float midtoneRange;
            int32_t calibrationMode;
        };

        AccumulateSpecData m_accSpecData{};
        std::vector<VkSpecializationMapEntry> m_accSpecMapEntries;
        VkSpecializationInfo m_accSpecInfo{};

        ReduceSpecData m_redSpecData{};
        std::vector<VkSpecializationMapEntry> m_redSpecMapEntries;
        VkSpecializationInfo m_redSpecInfo{};

        VkSampler m_sampler = VK_NULL_HANDLE;

        VkPipeline m_accumulatePipeline     = VK_NULL_HANDLE;
        VkPipeline m_reducePipeline         = VK_NULL_HANDLE;
        VkPipelineLayout m_accumulateLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_reduceLayout     = VK_NULL_HANDLE;

        VkBuffer m_histogramBuffer            = VK_NULL_HANDLE;
        VkBuffer m_temporalBuffer             = VK_NULL_HANDLE;
        VkBuffer m_metricsBuffer              = VK_NULL_HANDLE;
        VkBuffer m_stagingMetricsBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_stagingMetricsMemory = VK_NULL_HANDLE;
        void* m_mappedMetrics                 = nullptr;
        float m_lastPeak                      = -1.0f;
        float m_lastWhite                     = -1.0f;
        VkDeviceMemory m_histogramMemory      = VK_NULL_HANDLE;
        VkDeviceMemory m_temporalMemory       = VK_NULL_HANDLE;
        VkDeviceMemory m_metricsMemory        = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_accumulateSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_reduceSetLayout     = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_metricsSetLayout    = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool           = VK_NULL_HANDLE;

        std::vector<VkDescriptorSet> m_accumulateSets;
        std::vector<VkDescriptorSet> m_reduceSets;
        std::vector<VkDescriptorSet> m_metricsDescriptorSets;

        VkShaderModule m_accumulateModule = VK_NULL_HANDLE;
        VkShaderModule m_reduceModule     = VK_NULL_HANDLE;

        uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
        VkBuffer createBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkDeviceMemory& memory,
            VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    };

} // namespace vkBasalt
