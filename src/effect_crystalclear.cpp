#include "effect_crystalclear.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
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

    CrystalClearEffect::CrystalClearEffect(LogicalDevice*       pLogicalDevice,
                                           VkFormat             format,
                                           VkExtent2D           imageExtent,
                                           std::vector<VkImage> inputImages,
                                           std::vector<VkImage> outputImages,
                                           Config*              pConfig,
                                           VkColorSpaceKHR      colorSpace)
    {
        Logger::debug("in creating CrystalClearEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = crystalclear_frag;

        this->pushConstantSize = sizeof(CrystalClearPushConstants);
        needsUniformBuffer = true;
        uniformSize  = sizeof(FrameData);

        // devfav falls back to raw defaults by design
        std::string preset = pConfig->getOption<std::string>("crystalclearPreset", "devfav");
        Logger::debug("CrystalClear Preset: " + preset);
        {
            int idx = 0;
            for (const auto& p : getParamDescs()) {
                if (p.key == "crystalclearPreset") {
                    for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                        if (preset == p.comboOptions[ci]) { idx = (int)ci; break; }
                    }
                    break;
                }
            }
            m_paramValues["crystalclearPreset"] = (double)idx;
        }

        const auto& presetTable = getPresetTable();
        auto presetIt = presetTable.find(preset);

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        // Read config, apply presets once, clamp, write to specData by offset, and build mapEntries
        const auto& params = getParamDescs();

        const std::string appliedPresetKey = "crystalclearPresetApplied";
        const std::string lastAppliedPreset = pConfig->getOption<std::string>(appliedPresetKey, "");

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
                    size_t idx = std::min(
                        static_cast<size_t>(val),
                        p.comboOptions.empty() ? 0 : p.comboOptions.size() - 1
                    );

                    if (!p.comboOptions.empty()) {
                        pConfig->setOption(p.key, p.comboOptions[idx]);
                    }
                } else if (p.type == ParamType::Int || p.type == ParamType::Bool) {
                    pConfig->setOption(p.key, std::to_string(static_cast<int32_t>(val)));
                } else {
                    std::string s = std::to_string(val);
                    std::replace(s.begin(), s.end(), ',', '.');
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
            double val;

            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = 0;
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

            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                float f = (float)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &f, sizeof(float));
            } else {
                int32_t i = (int32_t)val;
                std::memcpy((uint8_t*)&specData + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back({(uint32_t)p.specId, (uint32_t)p.specOffset, p.specSize});
        }

        specData.colorSpaceMode = static_cast<int32_t>(csm);
        mapEntries.push_back({65535, offsetof(CrystalClearSpecData, colorSpaceMode), sizeof(int32_t)});

        this->radius = specData.radius;
        this->offset = specData.offset;

        float texelSizeX = 1.0f / static_cast<float>(imageExtent.width);
        float texelSizeY = 1.0f / static_cast<float>(imageExtent.height);

        float rawOffset  = 1.5f * radius * offset;
        float baseOffset = std::floor(rawOffset) + 0.5f;

        pushConstants.step1.x = baseOffset * texelSizeX;
        pushConstants.step1.y = baseOffset * texelSizeY;
        pushConstants.step2.x = pushConstants.step1.x * 3.0f;
        pushConstants.step2.y = pushConstants.step1.y * 3.0f;
        pushConstants.pixelSize.x = texelSizeX;
        pushConstants.pixelSize.y = texelSizeY;

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(CrystalClearSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    CrystalClearEffect::~CrystalClearEffect() {}

    void CrystalClearEffect::updateEffect() {
        if (mappedUniform) {
            FrameData* data = static_cast<FrameData*>(mappedUniform);
            data->frameCounter = m_frameCounter++;
        }
    }

    void CrystalClearEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) {
        // Barrier 1: Acquire inputImages for reading
        VkImageMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        // Render Pass (writes to outputImages, automatically transitions them to finalLayout = PRESENT_SRC_KHR)
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CrystalClearPushConstants), &pushConstants);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2: Restore inputImages layout after we're done reading it.
        VkImageMemoryBarrier secondBarrier = {};
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = isFirstInChain ? VK_ACCESS_MEMORY_READ_BIT : 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &secondBarrier);

        // Barrier 3: Prepare outputImages for the next consumer.
        if (!isLastInChain) {
            VkImageMemoryBarrier thirdBarrier = {};
            thirdBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            thirdBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            thirdBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            thirdBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            thirdBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            thirdBarrier.image               = outputImages[imageIndex];
            thirdBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &thirdBarrier);
        }
    }

} // namespace vkBasalt
