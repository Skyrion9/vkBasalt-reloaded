#include "effect_smaa.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_core.h>
#include "AreaTex.h"
#include "SearchTex.h"

#include "config.hpp"
#include "effect.hpp"
#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "image.hpp"
#include "util.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    struct SmaaOptions
    {
        float   screenWidth;
        float   screenHeight;
        float   reverseScreenWidth;
        float   reverseScreenHeight;
        float   threshold;
        int32_t maxSearchSteps;
        int32_t maxSearchStepsDiag;
        int32_t cornerRounding;
        int32_t disableDiagDetection;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(SmaaOptions, field), .specSize = sizeof(((SmaaOptions*)0)->field)

    SmaaEffect::SmaaEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig)
    {
        Logger::debug("in creating SmaaEffect");

        this->pLogicalDevice = pLogicalDevice;
        this->format         = format;
        this->imageExtent    = imageExtent;
        this->inputImages    = inputImages;
        this->outputImages   = outputImages;
        this->pConfig        = pConfig;

        std::vector<VkImage> edgeAndBlendImages = createImages(pLogicalDevice,
                                                               inputImages.size() * 2,
                                                               {imageExtent.width, imageExtent.height, 1},
                                                               VK_FORMAT_B8G8R8A8_UNORM,
                                                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               imageMemory);

        edgeImages  = std::vector<VkImage>(edgeAndBlendImages.begin(), edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2);
        blendImages = std::vector<VkImage>(edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2, edgeAndBlendImages.end());

        inputImageViews = createImageViews(pLogicalDevice, format, inputImages);
        Logger::debug("created input ImageViews");
        edgeImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, edgeImages);
        Logger::debug("created edge  ImageViews");
        blendImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, blendImages);
        Logger::debug("created blend ImageViews");
        outputImageViews = createImageViews(pLogicalDevice, format, outputImages);
        Logger::debug("created output ImageViews");

        sampler = createSampler(pLogicalDevice);
        Logger::debug("created sampler");

        VkExtent3D areaImageExtent = {AREATEX_WIDTH, AREATEX_HEIGHT, 1};
        areaImage = createImages(pLogicalDevice, 1, areaImageExtent, VK_FORMAT_R8G8_UNORM,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, areaMemory)[0];

        VkExtent3D searchImageExtent = {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1};
        searchImage = createImages(pLogicalDevice, 1, searchImageExtent, VK_FORMAT_R8_UNORM,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, searchMemory)[0];

        uploadToImage(pLogicalDevice, areaImage, areaImageExtent, AREATEX_SIZE, areaTexBytes);
        uploadToImage(pLogicalDevice, searchImage, searchImageExtent, SEARCHTEX_SIZE, searchTexBytes);

        areaImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8G8_UNORM, std::vector<VkImage>(1, areaImage))[0];
        Logger::debug("after creating area ImageView");
        searchImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8_UNORM, std::vector<VkImage>(1, searchImage))[0];
        Logger::debug("created search ImageView");

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 5);
        Logger::debug("created descriptorSetLayouts");

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = inputImages.size() * 5;
        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        descriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);
        Logger::debug("created descriptorPool");

        std::string preset = pConfig->getOption<std::string>("smaaPreset", "");
        std::string edgeDetection = pConfig->getOption<std::string>("smaaEdgeDetection", "luma");
    
        m_paramValues["smaaPreset"] = 0.0;
        m_paramValues["smaaEdgeDetection"] = 0.0;

        struct SmaaPreset {
            float threshold;
            int32_t maxSearchSteps;
            int32_t maxSearchStepsDiag;
            int32_t cornerRounding;
            int32_t disableDiagDetection;
        };

        SmaaPreset activePreset = {0.05f, 32, 16, 25, 0};
        if (preset == "low")         activePreset = {0.15f, 4,  0,  25, 1};
        else if (preset == "medium") activePreset = {0.10f, 8,  0,  25, 1};
        else if (preset == "high")   activePreset = {0.10f, 16, 8,  25, 0};
        else if (preset == "ultra")  activePreset = {0.05f, 32, 16, 25, 0};

        using PresetMap = std::unordered_map<std::string, double>;
        PresetMap presetDefaults = {
            {"smaaThreshold",            (double)activePreset.threshold},
            {"smaaMaxSearchSteps",       (double)activePreset.maxSearchSteps},
            {"smaaMaxSearchStepsDiag",   (double)activePreset.maxSearchStepsDiag},
            {"smaaCornerRounding",       (double)activePreset.cornerRounding},
            {"smaaDisableDiagDetection", (double)activePreset.disableDiagDetection},
        };

        const auto& params = getParamDescs();
        SmaaOptions smaaOptions = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double def = p.defaultVal;
            auto overrideIt = presetDefaults.find(p.key);
            if (overrideIt != presetDefaults.end()) {
                def = overrideIt->second;
            }

            double val;
            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = 0;
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) { idx = (int)ci; break; }
                }
                val = (double)idx;
            } else if (p.type == ParamType::Float) {
                val = (double)pConfig->getOption<float>(p.key, (float)def);
            } else {
                val = (double)pConfig->getOption<int32_t>(p.key, (int32_t)def);
            }

            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                float f = (float)val;
                std::memcpy((uint8_t*)&smaaOptions + p.specOffset, &f, sizeof(float));
            } else {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&smaaOptions + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        smaaOptions.screenWidth         = (float)imageExtent.width;
        smaaOptions.screenHeight        = (float)imageExtent.height;
        smaaOptions.reverseScreenWidth  = 1.0f / imageExtent.width;
        smaaOptions.reverseScreenHeight = 1.0f / imageExtent.height;

        mapEntries.push_back({0, offsetof(SmaaOptions, screenWidth),         sizeof(float)});
        mapEntries.push_back({1, offsetof(SmaaOptions, screenHeight),        sizeof(float)});
        mapEntries.push_back({2, offsetof(SmaaOptions, reverseScreenWidth),  sizeof(float)});
        mapEntries.push_back({3, offsetof(SmaaOptions, reverseScreenHeight), sizeof(float)});


        createShaderModule(pLogicalDevice, smaa_edge_vert, &edgeVertexModule);
        bool useColor = (edgeDetection == "color");
        auto shaderCode = useColor ? smaa_edge_color_frag : smaa_edge_luma_frag;
        createShaderModule(pLogicalDevice, shaderCode, &edgeFragmentModule);

        createShaderModule(pLogicalDevice, smaa_blend_vert, &blendVertexModule);
        createShaderModule(pLogicalDevice, smaa_blend_frag, &blendFragmentModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_vert, &neighborVertexModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_frag, &neighborFragmentModule);

        renderPass      = createRenderPass(pLogicalDevice, format, false, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        unormRenderPass = createRenderPass(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {imageSamplerDescriptorSetLayout};
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(smaaOptions);
        specializationInfo.pData         = &smaaOptions;

        edgePipeline = createGraphicsPipeline(pLogicalDevice, edgeVertexModule, &specializationInfo, "main",
                                              edgeFragmentModule, &specializationInfo, "main",
                                              imageExtent, unormRenderPass, pipelineLayout);

        blendPipeline = createGraphicsPipeline(pLogicalDevice, blendVertexModule, &specializationInfo, "main",
                                               blendFragmentModule, &specializationInfo, "main",
                                               imageExtent, unormRenderPass, pipelineLayout);

        neighborPipeline = createGraphicsPipeline(pLogicalDevice, neighborVertexModule, &specializationInfo, "main",
                                                  neighborFragmentModule, &specializationInfo, "main",
                                                  imageExtent, renderPass, pipelineLayout);

        std::vector<std::vector<VkImageView>> imageViewsVector = {
            inputImageViews, edgeImageViews,
            std::vector<VkImageView>(inputImageViews.size(), areaImageView),
            std::vector<VkImageView>(inputImageViews.size(), searchImageView),
            blendImageViews
        };

        imageDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice, descriptorPool,
                                                                         imageSamplerDescriptorSetLayout,
                                                                         std::vector<VkSampler>(imageViewsVector.size(), sampler),
                                                                         imageViewsVector);

        edgeFramebuffers     = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {edgeImageViews});
        blendFramebuffers    = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {blendImageViews});
        neighborFramebuffers = createFramebuffers(pLogicalDevice, renderPass, imageExtent, {outputImageViews});
    }

    void SmaaEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        // Barrier 1: inputImages -> SHADER_READ_ONLY
        VkImageMemoryBarrier barrier1 = {};
        barrier1.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier1.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier1.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier1.oldLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 
                                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.image               = inputImages[imageIndex];
        barrier1.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier1);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        // Alpha 1f
        VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassBeginInfo.clearValueCount   = 1;
        renderPassBeginInfo.pClearValues      = &clearValue;

        // Pass 1: edge detection
        renderPassBeginInfo.renderPass  = unormRenderPass;
        renderPassBeginInfo.framebuffer = edgeFramebuffers[imageIndex];

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, edgePipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: edge image memory visibility (Internal to SMAA)
        VkImageMemoryBarrier barrier2 = {};
        barrier2.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier2.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier2.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier2.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image               = edgeImages[imageIndex];
        barrier2.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier2);

        // Pass 2: blend weight calculation
        renderPassBeginInfo.framebuffer = blendFramebuffers[imageIndex];

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 3: blend image memory visibility (Internal to SMAA)
        VkImageMemoryBarrier barrier3 = {};
        barrier3.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier3.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier3.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier3.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier3.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier3.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier3.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier3.image               = blendImages[imageIndex];
        barrier3.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier3);

        // Pass 3: neighborhood blending (writes to outputImages)
        renderPassBeginInfo.framebuffer = neighborFramebuffers[imageIndex];
        renderPassBeginInfo.renderPass  = renderPass;
        renderPassBeginInfo.clearValueCount = 0;
        renderPassBeginInfo.pClearValues    = nullptr;
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, neighborPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 4: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier barrier4 = {};
        barrier4.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier4.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier4.dstAccessMask       = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        barrier4.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier4.newLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 
                                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier4.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.image               = inputImages[imageIndex];
        barrier4.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier4);

        // Barrier 5: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier barrier5 = {};
            barrier5.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier5.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier5.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier5.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier5.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier5.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier5.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier5.image               = outputImages[imageIndex];
            barrier5.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier5);
        }
    }

    const std::vector<EffectParamDesc>& SmaaEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "smaaPreset", .label = "Preset", .type = ParamType::Combo,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.0, .step = 0.0,
             .comboOptions = {"", "low", "medium", "high", "ultra"},
             .category = "Preset",
             .tooltip = "SMAA quality preset. Sets threshold, search steps, and diagonal detection defaults.\n"
                        "(empty): manual.\nlow: fast, minimal AA.\nmedium: balanced.\n"
                        "high: strong AA.\nultra: maximum quality, most expensive."},

            {.key = "smaaEdgeDetection", .label = "Edge Detection", .type = ParamType::Combo,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.0, .step = 0.0,
             .comboOptions = {"luma", "color"},
             .category = "Edge Detection",
             .tooltip = "Edge detection method.\nluma: luminance-only (faster, may miss chroma-only edges).\n"
                        "color: per-channel detection (catches more edges, slightly slower)."},

            {.key = "smaaThreshold", .label = "Threshold", .type = ParamType::Float,
             .defaultVal = 0.05, .minVal = 0.01, .maxVal = 0.5, .step = 0.01,
             .category = "Edge Detection", SPEC(4, threshold)},

            {.key = "smaaMaxSearchSteps", .label = "Max Search Steps", .type = ParamType::Int,
             .defaultVal = 32.0, .minVal = 0.0, .maxVal = 112.0, .step = 1.0,
             .category = "Search", SPEC(5, maxSearchSteps)},

            {.key = "smaaMaxSearchStepsDiag", .label = "Max Diag Steps", .type = ParamType::Int,
             .defaultVal = 16.0, .minVal = 0.0, .maxVal = 20.0, .step = 1.0,
             .category = "Search", SPEC(6, maxSearchStepsDiag)},

            {.key = "smaaCornerRounding", .label = "Corner Rounding", .type = ParamType::Int,
             .defaultVal = 25.0, .minVal = 0.0, .maxVal = 100.0, .step = 1.0,
             .category = "Anti-Aliasing", SPEC(7, cornerRounding)},

            {.key = "smaaDisableDiagDetection", .label = "Disable Diag Detection", .type = ParamType::Bool,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
             .category = "Search", SPEC(8, disableDiagDetection)},
        };
        return params;
    }

    SmaaEffect::~SmaaEffect()
    {
        Logger::debug("destroying smaa effect " + convertToString(this));
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, edgePipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, blendPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, neighborPipeline, nullptr);

        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, unormRenderPass, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);

        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, edgeVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, edgeFragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, blendVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, blendFragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, neighborVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, neighborFragmentModule, nullptr);

        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, imageMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, areaMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, searchMemory, nullptr);

        for (unsigned int i = 0; i < edgeFramebuffers.size(); i++)
        {
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, edgeFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, blendFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, neighborFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, edgeImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, blendImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, edgeImages[i], nullptr);
            pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, blendImages[i], nullptr);
        }
        Logger::debug("after SMAA DestroyImageView");
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, areaImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, areaImage, nullptr);
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, searchImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, searchImage, nullptr);
        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
    }
} // namespace vkBasalt
