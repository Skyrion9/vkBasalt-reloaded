#ifndef EFFECT_SIMPLE_HPP_INCLUDED
#define EFFECT_SIMPLE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"

#include "effect.hpp"
#include "config.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    // Lightweight vec2 to mirror GLSL's vec2 without needing GLM
    struct PushVec2 {
        float x;
        float y;
    };

    // UBO Struct for per-frame temporal data
    struct FrameData {
        uint32_t frameCounter;
    };

    class SimpleEffect : public Effect
    {
    public:
        SimpleEffect();
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        virtual ~SimpleEffect();

    protected:
        LogicalDevice*               pLogicalDevice;
        std::vector<VkImage>         inputImages;
        std::vector<VkImage>         outputImages;
        std::vector<VkImageView>     inputImageViews;
        std::vector<VkImageView>     outputImageViews;
        std::vector<VkDescriptorSet> imageDescriptorSets;
        std::vector<VkFramebuffer>   framebuffers;
        VkDescriptorSetLayout        imageSamplerDescriptorSetLayout;
        VkDescriptorPool             descriptorPool;
        VkShaderModule               vertexModule;
        VkShaderModule               fragmentModule;
        VkRenderPass                 renderPass;
        VkPipelineLayout             pipelineLayout;
        VkPipeline                   graphicsPipeline;
        VkExtent2D                   imageExtent;
        VkFormat                     format;
        VkSampler                    sampler;
        Config*                      pConfig;
        std::vector<uint32_t>        vertexCode;
        std::vector<uint32_t>        fragmentCode;
        VkSpecializationInfo*        pVertexSpecInfo;
        VkSpecializationInfo*        pFragmentSpecInfo;
        uint32_t                     pushConstantSize = 16; // subclasses can set this to the size of their push constants, safe default is 16 bytes.

        // UBO support for per-frame data (e.g., temporal frame counters)
        // Subclasses can set needsUniformBuffer = true and uniformSize = sizeof(Struct) in their constructor.
        VkBuffer uniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        void* mappedUniform = nullptr;
        size_t uniformSize = 0;
        bool needsUniformBuffer = false;
        
        bool needsClear = false;
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // subclasses can put DescriptorSets in here, but the first one will be the input image descriptorSet
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

        void init(LogicalDevice*       pLogicalDevice,
                  VkFormat             format,
                  VkExtent2D           imageExtent,
                  std::vector<VkImage> inputImages,
                  std::vector<VkImage> outputImages,
                  Config*              pConfig);
    };
} // namespace vkBasalt

#endif // EFFECT_SIMPLE_HPP_INCLUDED
