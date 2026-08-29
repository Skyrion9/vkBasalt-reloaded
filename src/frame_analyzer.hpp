#pragma once

#include <vector>
#include <array>

#include "compute_pass.hpp"
#include "simple_compute_pass.hpp"
#include "logical_swapchain.hpp"

namespace vkBasalt
{
    // GPU accelerated frame analyzer. Decodes to linear light, bins pixels into SSBOs. Reads SSBOs, normalizes, writes heat mapped images.
    // Exposes 3 resolved images (histogram, waveform, vectorscope) as VkDescriptorSets for ImGui to render directly without CPU readback.
    class FrameAnalyzer : public ComputePass
    {
    public:
        FrameAnalyzer(LogicalDevice* pDevice, VkExtent2D extent,
                      const std::vector<VkImage>& inputImages, VkFormat inputFormat,
                      VkColorSpaceKHR colorSpace);
        ~FrameAnalyzer() override;

        void recordCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        std::string getName() const override { return "frame_analyzer"; }

        enum ScopeType { HISTOGRAM = 0, WAVEFORM = 1, VECTORSCOPE = 2, SCOPE_COUNT = 3 };
        
        // Expose raw handles for ImGui backend registration
        VkImageView getScopeImageView(ScopeType type) const { return m_scopeViews[type]; }
        VkSampler   getScopeSampler() const { return m_sampler; }
        VkDescriptorSet getScopeDescriptorSet(ScopeType type) const { return m_imguiSets[type]; }

    private:
        LogicalDevice* m_pDevice;
        VkExtent2D m_extent;
        std::vector<VkImage> m_inputImages;
        std::vector<VkImageView> m_inputViews;
        int m_colorSpaceMode;

        // SSBOs: histogram (4KB), waveform (256KB), vectorscope (256KB)
        static constexpr VkDeviceSize HIST_SIZE  = 1024 * sizeof(uint32_t);
        static constexpr VkDeviceSize SCOPE_SIZE = 65536 * sizeof(uint32_t);

        VkBuffer m_histBuffer = VK_NULL_HANDLE;
        VkBuffer m_waveBuffer = VK_NULL_HANDLE;
        VkBuffer m_vecBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory m_histMemory = VK_NULL_HANDLE;
        VkDeviceMemory m_waveMemory = VK_NULL_HANDLE;
        VkDeviceMemory m_vecMemory  = VK_NULL_HANDLE;

        // Output images (256x256 R8G8B8A8_UNORM)
        static constexpr uint32_t SCOPE_DIM = 256;
        std::array<VkImage, SCOPE_COUNT>        m_scopeImages{};
        std::array<VkImageView, SCOPE_COUNT>    m_scopeViews{};
        std::array<VkDeviceMemory, SCOPE_COUNT> m_scopeMemory{};

        // Accumulate pipeline
        VkShaderModule        m_accumShader   = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_accumDSL      = VK_NULL_HANDLE;
        VkPipelineLayout      m_accumLayout   = VK_NULL_HANDLE;
        VkPipeline            m_accumPipeline = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_accumSets; // one per image

        // Resolve pipeline
        VkShaderModule        m_resolveShader   = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_resolveDSL      = VK_NULL_HANDLE;
        VkPipelineLayout      m_resolveLayout   = VK_NULL_HANDLE;
        VkPipeline            m_resolvePipeline = VK_NULL_HANDLE;
        VkDescriptorSet       m_resolveSet      = VK_NULL_HANDLE;

        // ImGui display (combined image samplers)
        VkDescriptorSetLayout m_imguiDSL   = VK_NULL_HANDLE;
        VkDescriptorPool      m_pool       = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, SCOPE_COUNT> m_imguiSets{};
        VkSampler             m_sampler    = VK_NULL_HANDLE;

        bool m_layoutsInitialized = false;

        struct PushConstants {
            uint32_t width;
            uint32_t height;
            uint32_t enabled;
        };
        PushConstants m_pushConstants;

        void createResources();
        void destroyResources();
    };
} // namespace vkBasalt
