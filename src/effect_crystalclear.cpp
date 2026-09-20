#include "effect_crystalclear.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <math.h>
#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "effect.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "shader_sources.hpp"
#include "util.hpp"
#include "format.hpp"

namespace vkBasalt
{

    CrystalClearEffect::CrystalClearEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace)
    {
        Logger::debug("in creating CrystalClearEffect");

        vertexCode   = decompressShaderCached(full_screen_triangle_vert);
        fragmentCode = decompressShaderCached(crystalclear_frag);

        this->pushConstantSize = 0;
        needsUniformBuffer     = true;
        uniformSize            = sizeof(FrameData);

        // devfav falls back to raw defaults by design
        auto preset = pConfig->getOption<std::string>("crystalclearPreset", "devfav");
        Logger::debug("CrystalClear Preset: " + preset);
        {
            int idx = 0;
            for (const auto& p : getParamDescs()) {
                if (p.key == "crystalclearPreset") {
                    for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                        if (preset == p.comboOptions[ci]) {
                            idx = static_cast<int>(ci);
                            break;
                        }
                    }
                    break;
                }
            }
            m_paramValues["crystalclearPreset"] = static_cast<double>(idx);
        }

        const auto& presetTable = getPresetTable();
        auto presetIt           = presetTable.find(preset);

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        // Read config, apply presets once, clamp, write to specData by offset, and build mapEntries
        const auto& params = getParamDescs();

        const std::string appliedPresetKey = "crystalclearPresetApplied";
        const auto lastAppliedPreset       = pConfig->getOption<std::string>(appliedPresetKey, "");

        if (preset != lastAppliedPreset) {
            Logger::debug("Applying CrystalClear preset baseline: " + preset);

            for (const auto& p : params) {
                if (p.key == "crystalclearPreset") continue;

                double val = p.defaultVal;
                if (presetIt != presetTable.end()) {
                    auto overrideIt = presetIt->second.find(p.key);
                    if (overrideIt != presetIt->second.end()) {
                        val = std::clamp(overrideIt->second, p.minVal, p.maxVal);
                    }
                }

                if (p.type == ParamType::Combo) {
                    size_t idx =
                        std::min(static_cast<size_t>(val), p.comboOptions.empty() ? 0 : p.comboOptions.size() - 1);

                    if (!p.comboOptions.empty()) {
                        pConfig->setOption(p.key, p.comboOptions[idx]);
                    }
                } else if (p.type == ParamType::Int || p.type == ParamType::Bool) {
                    pConfig->setOption(p.key, std::to_string(static_cast<int32_t>(val)));
                } else {
                    std::string s = std::to_string(val);
                    std::ranges::replace(s, ',', '.');
                    pConfig->setOption(p.key, s);
                }
            }

            pConfig->setOption(appliedPresetKey, preset);
        }

        CrystalClearSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double def = p.defaultVal;
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
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(def)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(def)));
            }

            val                  = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                auto f = static_cast<float>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specData) + p.specOffset, &f, sizeof(float));
            } else {
                auto i = static_cast<int32_t>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specData) + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back(
                {.constantID = static_cast<uint32_t>(p.specId),
                 .offset     = static_cast<uint32_t>(p.specOffset),
                 .size       = p.specSize});
        }

        mapEntries.push_back(
            {.constantID = 78, .offset = offsetof(CrystalClearSpecData, step1_x), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 79, .offset = offsetof(CrystalClearSpecData, step1_y), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 80, .offset = offsetof(CrystalClearSpecData, step2_x), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 81, .offset = offsetof(CrystalClearSpecData, step2_y), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 82, .offset = offsetof(CrystalClearSpecData, pixelSize_x), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 83, .offset = offsetof(CrystalClearSpecData, pixelSize_y), .size = sizeof(float)});

        specData.colorSpaceMode = static_cast<int32_t>(csm);
        mapEntries.push_back(
            {.constantID = 65535, .offset = offsetof(CrystalClearSpecData, colorSpaceMode), .size = sizeof(int32_t)});

        this->radius = specData.radius;
        this->offset = specData.offset;

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        float rawOffset  = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        specData.step1_x     = baseOffset * texelSizeX;
        specData.step1_y     = baseOffset * texelSizeY;
        specData.step2_x     = specData.step1_x * 3.0f;
        specData.step2_y     = specData.step1_y * 3.0f;
        specData.pixelSize_x = texelSizeX;
        specData.pixelSize_y = texelSizeY;

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(CrystalClearSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, std::move(inputImages), std::move(outputImages), pConfig);
    }

    CrystalClearEffect::~CrystalClearEffect() = default;

    void CrystalClearEffect::updateEffect()
    {
        if (mappedUniform) {
            auto* data         = static_cast<FrameData*>(mappedUniform);
            data->frameCounter = m_frameCounter++;
        }
    }

    void CrystalClearEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        // Barrier 1: Acquire inputImages for reading
        VkImageMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        memoryBarrier.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout =
            isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &memoryBarrier);

        // Render Pass (writes to outputImages, automatically transitions them to finalLayout = PRESENT_SRC_KHR)
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = renderPass;
        renderPassBeginInfo.framebuffer           = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset     = {.x = 0, .y = 0};
        renderPassBeginInfo.renderArea.extent     = imageExtent;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0,
            nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier secondBarrier = {};
        secondBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask        = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        secondBarrier.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout =
            isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
            0, nullptr, 1, &secondBarrier);

        // Barrier 3: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier thirdBarrier = {};
            thirdBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            thirdBarrier.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.oldLayout            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            thirdBarrier.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            thirdBarrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.image                = outputImages[imageIndex];
            thirdBarrier.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                nullptr, 1, &thirdBarrier);
        }
    }

} // namespace vkBasalt
