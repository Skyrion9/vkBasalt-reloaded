#include "compute_test_pass.hpp"
#include "shader_sources.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    ComputeTestPass::ComputeTestPass(LogicalDevice* pLogicalDevice, VkExtent2D extent,
                                     const std::vector<VkImage>& inputImages)
        : SimpleComputePass(pLogicalDevice)
        , m_extent(extent)
        , m_inputImages(inputImages)
    {
        m_pushConstants.width = extent.width;
        m_pushConstants.height = extent.height;

        // Histogram: 256 bins of uint32_t
        m_histogramBuffer = createDeviceLocalBuffer(256 * sizeof(uint32_t),
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        // Sampler
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        pLogicalDevice->vkd.CreateSampler(pLogicalDevice->device, &samplerInfo, nullptr, &m_sampler);

        // Input image views
        m_inputViews.resize(m_inputImages.size());
        for (size_t i = 0; i < m_inputImages.size(); i++)
            m_inputViews[i] = createImageView(m_inputImages[i], VK_FORMAT_UNDEFINED);
            // Note: VK_FORMAT_UNDEFINED in view inherits the image's format. If createImageView requires an explicit format, pass the swapchain format instead.

        init();
    }

    ComputeTestPass::~ComputeTestPass()
    {
        if (m_sampler != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, m_sampler, nullptr);
        for (auto v : m_inputViews)
            if (v != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, v, nullptr);
        // m_histogramBuffer memory is leaked here, track and free m_histogramMemory. The buffer itself is destroyed via the base class or here
        if (m_histogramBuffer != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, m_histogramBuffer, nullptr);
        if (m_histogramMemory != VK_NULL_HANDLE)
            pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, m_histogramMemory, nullptr);
    }

    const std::vector<uint32_t>& ComputeTestPass::getShaderCode() const
    {
        return compute_test_comp;
    }

    std::vector<VkDescriptorSetLayoutBinding> ComputeTestPass::getBindings() const
    {
        return {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        };
    }

    void ComputeTestPass::writeDescriptors(VkDescriptorSet set, uint32_t imageIndex)
    {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = m_sampler;
        imageInfo.imageView = m_inputViews[imageIndex];
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = m_histogramBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &imageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &bufferInfo;

        pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 2, writes, 0, nullptr);
    }

    void ComputeTestPass::getDispatchSize(uint32_t& x, uint32_t& y, uint32_t& z) const
    {
        // local_size_x = 8, local_size_y = 8
        x = (m_extent.width + 7) / 8;
        y = (m_extent.height + 7) / 8;
        z = 1;
    }
} // namespace vkBasalt
