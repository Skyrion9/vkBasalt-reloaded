#pragma once
#include "compute_pass.hpp"
#include <vector>

namespace vkBasalt
{
    // Base class for single dispatch compute passes. Handles all Vulkan boilerplate (pipeline, descriptors, push constants). 
    // Subclasses provide the shader, descriptor layout, push constant data, and dispatch dimensions.
    class SimpleComputePass : public ComputePass
    {
    public:
        SimpleComputePass(LogicalDevice* pLogicalDevice);
        ~SimpleComputePass() override;

        void recordCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    protected:
        // Return compute shader SPIR-V.
        virtual const std::vector<uint32_t>& getShaderCode() const = 0;
        // Return descriptor set layout bindings.
        virtual std::vector<VkDescriptorSetLayoutBinding> getBindings() const = 0;
        // Write descriptor set contents for the given image index.
        virtual void writeDescriptors(VkDescriptorSet set, uint32_t imageIndex) = 0;
        // Return push constant data and size (0 = no push constants).
        virtual const void* getPushConstants() const { return nullptr; }
        virtual uint32_t getPushConstantSize() const { return 0; }
        // Workgroup dispatch dimensions.
        virtual void getDispatchSize(uint32_t& x, uint32_t& y, uint32_t& z) const = 0;
        // Called after init completes. Use for creating additional resources.
        virtual void onInit() {}
        // Called before resource destruction. Use for cleaning up subclass resources.
        virtual void onDestroy() {}

        // Init helpers (call in subclass constructor)
        void init();

        VkBuffer createDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
        VkBuffer createHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                         VkDeviceMemory& memory, void** mapped);

        VkImage createImage(uint32_t width, uint32_t height, VkFormat format,
                            VkImageUsageFlags usage, VkDeviceMemory& memory);

        VkImageView createImageView(VkImage image, VkFormat format,
                                    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
                                    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

        // Transition image layouts during init (from UNDEFINED).
        void transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout,
                                   VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                   VkAccessFlags srcAccess, VkAccessFlags dstAccess);

        // State
        LogicalDevice* pLogicalDevice = nullptr;

        VkShaderModule         shaderModule       = VK_NULL_HANDLE;
        VkDescriptorSetLayout  descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool       descriptorPool     = VK_NULL_HANDLE;
        VkPipelineLayout       pipelineLayout     = VK_NULL_HANDLE;
        VkPipeline             pipeline           = VK_NULL_HANDLE;

        // One descriptor set per swapchain image (per image bindings differ).
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t pushConstantSize = 0;
        VkPushConstantRange pushConstantRange = {};
    };
} // namespace vkBasalt
