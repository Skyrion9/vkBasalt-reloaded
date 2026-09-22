///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2018, Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Original Author(s): Filip Strugar, Adam Lake (Intel Corporation)
// Original Source:    https://github.com/GameTechDev/CMAA2
//
// MODIFICATIONS BY Skyrion9/vkBasalt-reloaded:
// - Ported from DirectX 12 HLSL to Vulkan GLSL compute shaders.
// - Replaced per thread global atomics with GL_KHR_shader_subgroup_ballot reduction in canditates to reduce contention.
// - Added inplace slice optimization for the vkBasalt effect chain architecture.
// - Replaced compile time HLSL macros with Vulkan specialization constants with more configuration knobs.
// - Integrated with vkBasalt-reloaded's color_space.h for HDR (PQ/HLG) and SDR edge detection.
// - Removed MSAA support (vkBasalt-reloaded operates on the final resolved swapchain image).
// - Added cache routing qualifiers and conditional helper compilation for glslang.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
