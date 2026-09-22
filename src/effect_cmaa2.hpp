#ifndef EFFECT_CMAA2_HPP_INCLUDED
#define EFFECT_CMAA2_HPP_INCLUDED

#include "effect.hpp"
#include "config.hpp"
#include "logical_device.hpp"

#include <vector>
#include <string>
#include <unordered_map>

namespace vkBasalt
{

    class Cmaa2Effect : public Effect
    {
    public:
        using PresetMap = std::unordered_map<std::string, double>;

        Cmaa2Effect(
            LogicalDevice* pLogicalDevice,
            VkFormat unormFormat,
            VkExtent2D imageExtent,
            const std::vector<VkImage>& inputImages,
            std::vector<VkImage> outputImages,
            Config* pConfig,
            VkColorSpaceKHR colorSpace);

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        std::string getName() const override { return "cmaa2"; }
        bool supportsInPlace() const override { return true; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;

        ~Cmaa2Effect() override;

    private:
        LogicalDevice* pLogicalDevice;
        VkExtent2D imageExtent{};
        std::vector<VkImage> inputImages;
        std::vector<VkImage> outputImages;

        // Per image views
        std::vector<VkImageView> inputImageViews;
        std::vector<VkImageView> outputStorageViews;
        VkSampler sampler = VK_NULL_HANDLE;

        // Shared working images
        VkImage edgeImage                       = VK_NULL_HANDLE;
        VkImage deferredBlendHeadsImage         = VK_NULL_HANDLE;
        VkImageView edgeImageView               = VK_NULL_HANDLE;
        VkImageView deferredBlendHeadsImageView = VK_NULL_HANDLE;
        VkDeviceMemory edgeMemory               = VK_NULL_HANDLE;
        VkDeviceMemory deferredBlendHeadsMemory = VK_NULL_HANDLE;

        // Shared working buffers
        VkBuffer shapeCandidatesBuffer                 = VK_NULL_HANDLE;
        VkBuffer deferredBlendItemListBuffer           = VK_NULL_HANDLE;
        VkBuffer deferredBlendLocationListBuffer       = VK_NULL_HANDLE;
        VkBuffer controlBuffer                         = VK_NULL_HANDLE;
        VkBuffer executeIndirectBuffer                 = VK_NULL_HANDLE;
        VkDeviceMemory shapeCandidatesMemory           = VK_NULL_HANDLE;
        VkDeviceMemory deferredBlendItemListMemory     = VK_NULL_HANDLE;
        VkDeviceMemory deferredBlendLocationListMemory = VK_NULL_HANDLE;
        VkDeviceMemory controlMemory                   = VK_NULL_HANDLE;
        VkDeviceMemory executeIndirectMemory           = VK_NULL_HANDLE;

        // Compute pipelines
        VkPipeline edgesPipeline               = VK_NULL_HANDLE;
        VkPipeline dispatchArgsPipeline        = VK_NULL_HANDLE;
        VkPipeline processCandidatesPipeline   = VK_NULL_HANDLE;
        VkPipeline deferredApplyPipeline       = VK_NULL_HANDLE;
        VkPipeline debugEdgesPipeline          = VK_NULL_HANDLE;
        VkShaderModule edgesModule             = VK_NULL_HANDLE;
        VkShaderModule debugEdgesModule        = VK_NULL_HANDLE;
        VkShaderModule dispatchArgsModule      = VK_NULL_HANDLE;
        VkShaderModule processCandidatesModule = VK_NULL_HANDLE;
        VkShaderModule deferredApplyModule     = VK_NULL_HANDLE;

        // Compute descriptor set layout & pool
        VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool computeDescriptorPool           = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> computeDescriptorSets;
        VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;

        // Dummy output images for unused format variant bindings (validation compliance)
        VkImage dummyOutputImages[3]        = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkImageView dummyOutputViews[3]     = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDeviceMemory dummyOutputMemory[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

        struct Cmaa2SpecData
        {
            float edgeThreshold;
            float localContrastAdaptation;
            float simpleShapeBluriness;
            int32_t formatVariant;
            int32_t debugAA;
            int32_t debugEdges;
            int32_t extraSharpness;
            uint32_t resX;
            uint32_t resY;
            uint32_t maxLineLength;
            float minShapeLength;
            float armRatio;
            int32_t colorSpaceMode;
            int32_t edgeDetectionMode;
        };

        Cmaa2SpecData specData{};

        enum class DeferredApplyVariant { RGBA8, RGB10A2, RGBA16F };
        DeferredApplyVariant applyVariant;
    };

} // namespace vkBasalt
#endif
