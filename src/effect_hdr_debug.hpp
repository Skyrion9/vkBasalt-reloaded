#pragma once
#include "effect.hpp"
#include "config.hpp"
#include "logical_device.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>

namespace vkBasalt
{

    extern std::atomic<bool> g_hdrDebugToolActive;

    class HdrDebugEffect : public Effect
    {
    public:
        HdrDebugEffect(
            LogicalDevice* pLogicalDevice,
            VkFormat format,
            VkExtent2D imageExtent,
            const std::vector<VkImage>& outputImages,
            Config* pConfig);
        ~HdrDebugEffect() override;

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void updateEffect() override {}

        static std::atomic<float> s_debugPeakNits;
        static std::atomic<float> s_debugWhiteNits;
        static std::atomic<float> s_windowSize;
        static std::atomic<int> s_patternType;

    private:
        LogicalDevice* m_dev;
        VkExtent2D m_extent{};
        std::vector<VkImage> m_outImages;
        std::vector<VkImageView> m_outImageViews;

        VkPipeline m_computePipeline      = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_descPool       = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_descSets;

        struct UboData
        {
            float peakNits;
            float windowSize;
            int patternType;
        };
    };

} // namespace vkBasalt
