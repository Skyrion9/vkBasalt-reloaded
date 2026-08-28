#pragma once
#include "simple_compute_pass.hpp"
#include "logical_swapchain.hpp"
#include <vector>

namespace vkBasalt
{
    // Minimal compute pass for validating the SimpleComputePass infrastructure. Samples the input image at pixel centers and accumulates a 256-bin luminance histogram into SSBO. Produces no visible output.
    class ComputeTestPass : public SimpleComputePass
    {
    public:
        ComputeTestPass(LogicalDevice* pLogicalDevice, VkExtent2D extent,
                        const std::vector<VkImage>& inputImages);
        ~ComputeTestPass() override;

        std::string getName() const override { return "compute_test"; }

    protected:
        const std::vector<uint32_t>& getShaderCode() const override;
        std::vector<VkDescriptorSetLayoutBinding> getBindings() const override;
        void writeDescriptors(VkDescriptorSet set, uint32_t imageIndex) override;
        const void* getPushConstants() const override { return &m_pushConstants; }
        uint32_t getPushConstantSize() const override { return sizeof(m_pushConstants); }
        void getDispatchSize(uint32_t& x, uint32_t& y, uint32_t& z) const override;

    private:
        VkExtent2D m_extent;
        std::vector<VkImage> m_inputImages;
        std::vector<VkImageView> m_inputViews;
        VkBuffer m_histogramBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_histogramMemory = VK_NULL_HANDLE;
        VkSampler m_sampler = VK_NULL_HANDLE;

        struct {
            uint32_t width;
            uint32_t height;
        } m_pushConstants;
    };
} // namespace vkBasalt
