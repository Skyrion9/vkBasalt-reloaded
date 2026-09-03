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
            int32_t hdrAdaptive;
        };

        NitCalibrationSpecData m_specData;
        std::vector<VkSpecializationMapEntry> m_specMapEntries;
        VkSpecializationInfo m_specInfo;
        std::unique_ptr<AutoHdrAnalyzer> m_autoHdrAnalyzer;
        bool m_hdrAdaptive = true;
    };
} // namespace vkBasalt
