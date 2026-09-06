#pragma once
#include "effect_simple.hpp"

#include <vulkan/vulkan_core.h>

#include <vector>
#include <memory>


#include "config.hpp"
#include "format.hpp"
#include "auto_hdr_analyzer.hpp"

namespace vkBasalt {

    class NitCalibrationEffect : public SimpleEffect 
    {
    public:
        NitCalibrationEffect(LogicalDevice* pLogicalDevice, 
                             VkFormat sourceFormat, VkFormat destFormat, VkExtent2D imageExtent,
                             std::vector<VkImage> inputImages, std::vector<VkImage> outputImages,
                             Config* pConfig, 
                             VkColorSpaceKHR sourceColorSpace, VkColorSpaceKHR destColorSpace,
                             bool autoHdrActive);
        ~NitCalibrationEffect() override;
        
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void updateEffect() override;
        const std::vector<EffectParamDesc>& getParamDescs() const override { return getCalibrationParams(); }
        
        std::string getName() const override { return "nitcalibration"; }
        static const std::vector<EffectParamDesc>& getCalibrationParams();

    private:
        Config* m_pConfigRef;
        bool m_autoHdrActive;

        struct NitCalibrationSpecData {
            float sdrWhitePoint;
            float hdrPeakNits;
            int32_t autoHdrEnabled;
            int32_t toneMapperMode;
            int32_t sourceColorSpace;
            int32_t destColorSpace;
        };

        NitCalibrationSpecData m_specData;
        std::vector<VkSpecializationMapEntry> m_specMapEntries;
        VkSpecializationInfo m_specInfo;
        std::unique_ptr<AutoHdrAnalyzer> m_autoHdrAnalyzer;

        // Dummy metrics buffer for when adaptive is off
        VkDescriptorSetLayout m_dummyMetricsSetLayout = VK_NULL_HANDLE;
        VkBuffer m_dummyMetricsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_dummyMetricsMemory = VK_NULL_HANDLE;
        VkDescriptorPool m_dummyMetricsPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_dummyMetricsSets;
        
        float m_dummyMetricsData[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bool m_dummyMetricsInitialized = false;
        bool m_hdrAdaptive = true;
    };
} // namespace vkBasalt
