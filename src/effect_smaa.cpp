#include "effect_smaa.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <math.h>
#include <vulkan/vulkan_core.h>

#include "texture_data.hpp"
#include "shader_decompress.hpp"

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
#include "format.hpp"

namespace vkBasalt
{
#define SPEC(id, field) \
    .specId = (id), .specOffset = offsetof(SmaaOptions, field), .specSize = sizeof(((SmaaOptions*) 0)->field)

    static const std::unordered_map<std::string, SmaaEffect::PresetMap>& getPresetTable()
    {
        static const std::unordered_map<std::string, SmaaEffect::PresetMap> table = {
            {"low",
             {
                 {"smaaThreshold", 0.15},
                 {"smaaMaxSearchSteps", 8.0},
                 {"smaaMaxSearchStepsDiag", 0.0},
                 {"smaaCornerRounding", 25.0},
                 {"smaaDisableDiagDetection", 1.0},
             }},
            {"medium",
             {
                 {"smaaThreshold", 0.10},
                 {"smaaMaxSearchSteps", 16.0},
                 {"smaaMaxSearchStepsDiag", 0.0},
                 {"smaaCornerRounding", 25.0},
                 {"smaaDisableDiagDetection", 1.0},
             }},
            {"high",
             {
                 {"smaaThreshold", 0.05},
                 {"smaaMaxSearchSteps", 32.0},
                 {"smaaMaxSearchStepsDiag", 16.0},
                 {"smaaCornerRounding", 25.0},
                 {"smaaDisableDiagDetection", 0.0},
             }},
            {"ultra",
             {
                 {"smaaThreshold", 0.05},
                 {"smaaMaxSearchSteps", 48.0},
                 {"smaaMaxSearchStepsDiag", 20.0},
                 {"smaaCornerRounding", 25.0},
                 {"smaaDisableDiagDetection", 0.0},
             }},
        };
        return table;
    }

    SmaaEffect::SmaaEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        const std::vector<VkImage>& inputImages,
        const std::vector<VkImage>& outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace) :
        pLogicalDevice(pLogicalDevice), inputImages(inputImages), outputImages(outputImages), imageExtent(imageExtent),
        sampler(createSampler(pLogicalDevice))
    {
        Logger::debug("in creating SmaaEffect");
        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        std::vector<VkImage> edgeAndBlendImages = createImages(
            pLogicalDevice, inputImages.size() * 2,
            {.width = imageExtent.width, .height = imageExtent.height, .depth = 1}, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            imageMemory);

        edgeImages = std::vector<VkImage>(
            edgeAndBlendImages.begin(), edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2);
        blendImages =
            std::vector<VkImage>(edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2, edgeAndBlendImages.end());

        inputImageViews = createImageViews(pLogicalDevice, format, inputImages);
        Logger::debug("created input ImageViews");
        edgeImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, edgeImages);
        Logger::debug("created edge  ImageViews");
        blendImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, blendImages);
        Logger::debug("created blend ImageViews");
        outputImageViews = createImageViews(pLogicalDevice, format, outputImages);
        Logger::debug("created output ImageViews");

        Logger::debug("created sampler");

        VkExtent3D areaImageExtent = {.width = vkBasalt::areaTex_WIDTH, .height = vkBasalt::areaTex_HEIGHT, .depth = 1};
        areaImage                  = createImages(
            pLogicalDevice, 1, areaImageExtent, VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            areaMemory)[0];

        VkExtent3D searchImageExtent = {
            .width = vkBasalt::searchTex_WIDTH, .height = vkBasalt::searchTex_HEIGHT, .depth = 1};
        searchImage = createImages(
            pLogicalDevice, 1, searchImageExtent, VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            searchMemory)[0];

        auto areaBytes = decompressData(vkBasalt::areaTex_zst, vkBasalt::areaTex_zst_size, vkBasalt::areaTex_size);
        auto searchBytes =
            decompressData(vkBasalt::searchTex_zst, vkBasalt::searchTex_zst_size, vkBasalt::searchTex_size);

        if (areaBytes.empty() || searchBytes.empty()) {
            Logger::err("Failed to decompress SMAA lookup textures");
            return;
        }

        uploadToImage(pLogicalDevice, areaImage, areaImageExtent, vkBasalt::areaTex_size, areaBytes.data());
        uploadToImage(pLogicalDevice, searchImage, searchImageExtent, vkBasalt::searchTex_size, searchBytes.data());

        areaImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8G8_UNORM, std::vector<VkImage>(1, areaImage))[0];
        Logger::debug("after creating area ImageView");
        searchImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8_UNORM, std::vector<VkImage>(1, searchImage))[0];
        Logger::debug("created search ImageView");

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 5);
        Logger::debug("created descriptorSetLayouts");

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type                          = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount               = inputImages.size() * 5;
        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        descriptorPool                              = createDescriptorPool(pLogicalDevice, poolSizes);
        Logger::debug("created descriptorPool");

        // Stage 1: Resolve combo indices
        auto presetStr     = pConfig->getOption<std::string>("smaaPreset", "high");
        auto edgeDetection = pConfig->getOption<std::string>("smaaEdgeDetection", "luma");

        const auto& params = getParamDescs();

        // Set combo indices so the UI reflects the actual selection
        for (const auto& p : params) {
            if (p.key == "smaaPreset" || p.key == "smaaEdgeDetection") {
                const std::string& strVal = (p.key == "smaaPreset") ? presetStr : edgeDetection;
                int idx                   = 0;
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) {
                        idx = static_cast<int>(ci);
                        break;
                    }
                }
                m_paramValues[p.key] = static_cast<double>(idx);
            }
        }

        // Stage 2: Apply preset to config (if changed)
        const auto& presetTable = getPresetTable();
        auto presetIt           = presetTable.find(presetStr);

        const std::string appliedPresetKey = "smaaPresetApplied";
        const auto lastAppliedPreset       = pConfig->getOption<std::string>(appliedPresetKey, "");

        if (presetStr != lastAppliedPreset) {
            Logger::debug("Applying SMAA preset baseline: " + presetStr);
            for (const auto& p : params) {
                if (p.key == "smaaPreset" || p.key == "smaaEdgeDetection") continue;

                double val = p.defaultVal;
                if (presetIt != presetTable.end()) {
                    auto overrideIt = presetIt->second.find(p.key);
                    if (overrideIt != presetIt->second.end()) {
                        val = std::clamp(overrideIt->second, p.minVal, p.maxVal);
                    }
                }

                if (p.type == ParamType::Float) {
                    std::string s = std::to_string(val);
                    std::ranges::replace(s, ',', '.');
                    pConfig->setOption(p.key, s);
                } else {
                    pConfig->setOption(p.key, std::to_string(static_cast<int32_t>(val)));
                }
            }
            pConfig->setOption(appliedPresetKey, presetStr);
        }

        // Stage 3: Generic param loading loop
        SmaaOptions smaaOptions = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 5);

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val = NAN;
            if (p.type == ParamType::Combo) {
                auto strVal = pConfig->getOption<std::string>(p.key, "");
                int idx     = 0;
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) {
                        idx = static_cast<int>(ci);
                        break;
                    }
                }
                val = static_cast<double>(idx);
            } else if (p.type == ParamType::Float) {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(p.defaultVal)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(p.defaultVal)));
            }

            val                  = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                auto f = static_cast<float>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&smaaOptions) + p.specOffset, &f, sizeof(float));
            } else {
                auto i = static_cast<int32_t>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&smaaOptions) + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back(
                {.constantID = static_cast<uint32_t>(p.specId),
                 .offset     = static_cast<uint32_t>(p.specOffset),
                 .size       = p.specSize});
        }

        smaaOptions.screenWidth         = static_cast<float>(imageExtent.width);
        smaaOptions.screenHeight        = static_cast<float>(imageExtent.height);
        smaaOptions.reverseScreenWidth  = 1.0f / imageExtent.width;
        smaaOptions.reverseScreenHeight = 1.0f / imageExtent.height;
        smaaOptions.colorSpaceMode      = static_cast<int32_t>(csm);

        mapEntries.push_back({.constantID = 0, .offset = offsetof(SmaaOptions, screenWidth), .size = sizeof(float)});
        mapEntries.push_back({.constantID = 1, .offset = offsetof(SmaaOptions, screenHeight), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 2, .offset = offsetof(SmaaOptions, reverseScreenWidth), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 3, .offset = offsetof(SmaaOptions, reverseScreenHeight), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 65535, .offset = offsetof(SmaaOptions, colorSpaceMode), .size = sizeof(int32_t)});

        createShaderModule(pLogicalDevice, smaa_edge_vert, &edgeVertexModule);
        bool useColorEdgeDetection = (edgeDetection == "color");
        const auto& shaderCode =
            decompressShaderCached(useColorEdgeDetection ? smaa_edge_color_frag : smaa_edge_luma_frag);
        createShaderModule(pLogicalDevice, shaderCode, &edgeFragmentModule);

        createShaderModule(pLogicalDevice, smaa_blend_vert, &blendVertexModule);
        createShaderModule(pLogicalDevice, smaa_blend_frag, &blendFragmentModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_vert, &neighborVertexModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_frag, &neighborFragmentModule);

        renderPass = createRenderPass(pLogicalDevice, format, false, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        // Opt: 'false' sets LOAD_OP_DONT_CARE. The edge and blend shaders explicitly write 0.0 to non edge pixels, the hardware clear is redundant here.
        unormRenderPass =
            createRenderPass(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {imageSamplerDescriptorSetLayout};
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(smaaOptions);
        specializationInfo.pData         = &smaaOptions;

        edgePipeline = createGraphicsPipeline(
            pLogicalDevice, edgeVertexModule, &specializationInfo, "main", edgeFragmentModule, &specializationInfo,
            "main", imageExtent, unormRenderPass, pipelineLayout);

        blendPipeline = createGraphicsPipeline(
            pLogicalDevice, blendVertexModule, &specializationInfo, "main", blendFragmentModule, &specializationInfo,
            "main", imageExtent, unormRenderPass, pipelineLayout);

        neighborPipeline = createGraphicsPipeline(
            pLogicalDevice, neighborVertexModule, &specializationInfo, "main", neighborFragmentModule,
            &specializationInfo, "main", imageExtent, renderPass, pipelineLayout);

        std::vector<std::vector<VkImageView>> imageViewsVector = {
            inputImageViews, edgeImageViews, std::vector<VkImageView>(inputImageViews.size(), areaImageView),
            std::vector<VkImageView>(inputImageViews.size(), searchImageView), blendImageViews};

        imageDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(
            pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout,
            std::vector<VkSampler>(imageViewsVector.size(), sampler), imageViewsVector);

        edgeFramebuffers     = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {edgeImageViews});
        blendFramebuffers    = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {blendImageViews});
        neighborFramebuffers = createFramebuffers(pLogicalDevice, renderPass, imageExtent, {outputImageViews});
    }

    void SmaaEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        // Barrier 1: inputImages -> SHADER_READ_ONLY
        VkImageMemoryBarrier barrier1 = {};
        barrier1.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier1.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier1.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        barrier1.oldLayout =
            isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.image               = inputImages[imageIndex];
        barrier1.subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &barrier1);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderArea.offset     = {.x = 0, .y = 0};
        renderPassBeginInfo.renderArea.extent     = imageExtent;
        renderPassBeginInfo.clearValueCount       = 0;
        renderPassBeginInfo.pClearValues          = nullptr;

        // Pass 1: edge detection
        renderPassBeginInfo.renderPass  = unormRenderPass;
        renderPassBeginInfo.framebuffer = edgeFramebuffers[imageIndex];

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0,
            nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, edgePipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: edge image memory visibility (Internal to SMAA)
        VkImageMemoryBarrier barrier2 = {};
        barrier2.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier2.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier2.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        barrier2.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image                = edgeImages[imageIndex];
        barrier2.subresourceRange     = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &barrier2);

        // Pass 2: blend weight calculation
        renderPassBeginInfo.framebuffer = blendFramebuffers[imageIndex];

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 3: blend image memory visibility (Internal to SMAA)
        VkImageMemoryBarrier barrier3 = {};
        barrier3.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier3.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier3.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        barrier3.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier3.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier3.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier3.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier3.image                = blendImages[imageIndex];
        barrier3.subresourceRange     = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &barrier3);

        // Pass 3: neighborhood blending (writes to outputImages)
        renderPassBeginInfo.framebuffer     = neighborFramebuffers[imageIndex];
        renderPassBeginInfo.renderPass      = renderPass;
        renderPassBeginInfo.clearValueCount = 0;
        renderPassBeginInfo.pClearValues    = nullptr;
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, neighborPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 4: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier barrier4 = {};
        barrier4.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier4.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        barrier4.dstAccessMask        = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        barrier4.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier4.newLayout =
            isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier4.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.image               = inputImages[imageIndex];
        barrier4.subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
            0, nullptr, 1, &barrier4);

        // Barrier 5: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier barrier5 = {};
            barrier5.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier5.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier5.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier5.oldLayout            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier5.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier5.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier5.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier5.image                = outputImages[imageIndex];
            barrier5.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                nullptr, 1, &barrier5);
        }
    }

    const std::vector<EffectParamDesc>& SmaaEffect::getParamDescs() const
    {
        static const std::vector<EffectParamDesc> params = {
            {.key          = "smaaPreset",
             .label        = "Preset",
             .type         = ParamType::Combo,
             .defaultVal   = 3.0,
             .minVal       = 0.0,
             .maxVal       = 4.0,
             .step         = 1.0,
             .comboOptions = {"", "low", "medium", "high", "ultra"},
             .category     = "Preset",
             .tooltip =
                 "SMAA quality preset. Sets threshold, search steps, and diagonal detection defaults.\n"
                 "(empty): manual.\nlow: fast, consider FXAA/CMAA2, no diagonals.\nmedium: good quality, no "
                 "diagonals.\n"
                 "high: strong AA with diagonals (default).\nultra: wasteful, maximum quality, extended search."},

            {.key          = "smaaEdgeDetection",
             .label        = "Edge Detection",
             .type         = ParamType::Combo,
             .defaultVal   = 0.0,
             .minVal       = 0.0,
             .maxVal       = 0.0,
             .step         = 0.0,
             .comboOptions = {"luma", "color"},
             .category     = "Edge Detection",
             .tooltip      = "Edge detection method.\nluma: luminance-only (faster, may miss chroma-only edges).\n"
                             "color: per-channel detection (catches more edges, slightly slower)."},

            {.key        = "smaaThreshold",
             .label      = "Threshold",
             .type       = ParamType::Float,
             .defaultVal = 0.05,
             .minVal     = 0.01,
             .maxVal     = 0.5,
             .step       = 0.01,
             .category   = "Edge Detection",
             SPEC(4, threshold)},

            {.key        = "smaaMaxSearchSteps",
             .label      = "Max Search Steps",
             .type       = ParamType::Int,
             .defaultVal = 32.0,
             .minVal     = 0.0,
             .maxVal     = 112.0,
             .step       = 1.0,
             .category   = "Search",
             SPEC(5, maxSearchSteps)},

            {.key        = "smaaMaxSearchStepsDiag",
             .label      = "Max Diag Steps",
             .type       = ParamType::Int,
             .defaultVal = 16.0,
             .minVal     = 0.0,
             .maxVal     = 20.0,
             .step       = 1.0,
             .category   = "Search",
             SPEC(6, maxSearchStepsDiag)},

            {.key        = "smaaCornerRounding",
             .label      = "Corner Rounding",
             .type       = ParamType::Int,
             .defaultVal = 25.0,
             .minVal     = 0.0,
             .maxVal     = 100.0,
             .step       = 1.0,
             .category   = "Anti-Aliasing",
             SPEC(7, cornerRounding)},

            {.key        = "smaaDisableDiagDetection",
             .label      = "Disable Diag Detection",
             .type       = ParamType::Bool,
             .defaultVal = 0.0,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 1.0,
             .category   = "Search",
             SPEC(8, disableDiagDetection)},
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
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(
            pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);

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

        for (unsigned int i = 0; i < edgeFramebuffers.size(); i++) {
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
