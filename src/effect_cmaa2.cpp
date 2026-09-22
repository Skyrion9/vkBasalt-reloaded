#include "effect_cmaa2.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <math.h>
#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "image_view.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "shader.hpp"
#include "sampler.hpp"

#include "shader_sources.hpp"
#include "format.hpp"

namespace vkBasalt
{

#define SPEC(id, field) \
    .specId = (id), .specOffset = offsetof(Cmaa2Effect::Cmaa2SpecData, field), \
    .specSize = sizeof(((Cmaa2Effect::Cmaa2SpecData*) 0)->field)

    static const std::unordered_map<std::string, Cmaa2Effect::PresetMap>& getPresetTable()
    {
        static const std::unordered_map<std::string, Cmaa2Effect::PresetMap> presetTable = {
            {"Low",
             {
                 {"cmaa2MaxLineLength", 32.0},
                 {"cmaa2MinShapeLength", 8.0},
                 {"cmaa2ArmRatio", 1.10},
             }},
            {"Medium",
             {
                 {"cmaa2MaxLineLength", 64.0},
                 {"cmaa2MinShapeLength", 6.0},
                 {"cmaa2ArmRatio", 1.20},
             }},
            {"High",
             {
                 {"cmaa2MaxLineLength", 86.0},
                 {"cmaa2MinShapeLength", 5.0},
                 {"cmaa2ArmRatio", 1.25},
             }},
            {"Ultra",
             {
                 {"cmaa2MaxLineLength", 128.0},
                 {"cmaa2MinShapeLength", 3.0},
                 {"cmaa2ArmRatio", 2.00},
             }},
        };
        return presetTable;
    }

    static uint32_t findDeviceLocalMemoryType(LogicalDevice* pDevice, uint32_t typeBits)
    {
        for (uint32_t i = 0; i < pDevice->memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & (1 << i))
                && (pDevice->memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                       == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                return i;
        }
        return 0;
    }

    static VkBuffer
    createDeviceLocalBuffer(LogicalDevice* pDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory& memory)
    {
        VkBufferCreateInfo info = {};
        info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size               = size;
        info.usage              = usage;
        info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer         = VK_NULL_HANDLE;
        if (pDevice->vkd.CreateBuffer(pDevice->device, &info, nullptr, &buffer) != VK_SUCCESS) return VK_NULL_HANDLE;
        VkMemoryRequirements memReqs;
        pDevice->vkd.GetBufferMemoryRequirements(pDevice->device, buffer, &memReqs);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize       = memReqs.size;
        allocInfo.memoryTypeIndex      = findDeviceLocalMemoryType(pDevice, memReqs.memoryTypeBits);
        if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            pDevice->vkd.DestroyBuffer(pDevice->device, buffer, nullptr);
            return VK_NULL_HANDLE;
        }
        pDevice->vkd.BindBufferMemory(pDevice->device, buffer, memory, 0);
        return buffer;
    }

    static VkImage createDeviceLocalImage(
        LogicalDevice* pDevice,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkDeviceMemory& memory)
    {
        VkImageCreateInfo info = {};
        info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType         = VK_IMAGE_TYPE_2D;
        info.format            = format;
        info.extent            = {.width = width, .height = height, .depth = 1};
        info.mipLevels         = 1;
        info.arrayLayers       = 1;
        info.samples           = VK_SAMPLE_COUNT_1_BIT;
        info.tiling            = VK_IMAGE_TILING_OPTIMAL;
        info.usage             = usage;
        info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage image          = VK_NULL_HANDLE;
        if (pDevice->vkd.CreateImage(pDevice->device, &info, nullptr, &image) != VK_SUCCESS) return VK_NULL_HANDLE;
        VkMemoryRequirements memReqs;
        pDevice->vkd.GetImageMemoryRequirements(pDevice->device, image, &memReqs);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize       = memReqs.size;
        allocInfo.memoryTypeIndex      = findDeviceLocalMemoryType(pDevice, memReqs.memoryTypeBits);
        if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            pDevice->vkd.DestroyImage(pDevice->device, image, nullptr);
            return VK_NULL_HANDLE;
        }
        pDevice->vkd.BindImageMemory(pDevice->device, image, memory, 0);
        return image;
    }

    static VkImageView createImageViewHelper(LogicalDevice* pDevice, VkImage image, VkFormat format)
    {
        VkImageViewCreateInfo info = {};
        info.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                 = image;
        info.viewType              = VK_IMAGE_VIEW_TYPE_2D;
        info.format                = format;
        info.subresourceRange      = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};
        VkImageView view = VK_NULL_HANDLE;
        pDevice->vkd.CreateImageView(pDevice->device, &info, nullptr, &view);
        return view;
    }

    Cmaa2Effect::Cmaa2Effect(
        LogicalDevice* pLogicalDevice,
        VkFormat unormFormat,
        VkExtent2D imageExtent,
        const std::vector<VkImage>& inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace) :
        pLogicalDevice(pLogicalDevice), imageExtent(imageExtent), inputImages(inputImages), outputImages(outputImages)
    {
        Logger::debug("in creating Cmaa2Effect (true Intel CMAA2, direct-write)");

        const uint32_t resX = imageExtent.width;
        const uint32_t resY = imageExtent.height;

        Cmaa2SpecData specDataLocal = {};

        // Stage 1: Resolve combo indices and non-spec params
        auto presetStr    = pConfig->getOption<std::string>("cmaa2QualityPreset", "Ultra");
        int qualityPreset = 3; // Default Ultra
        for (const auto& p : getParamDescs()) {
            if (p.key == "cmaa2QualityPreset") {
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == presetStr) {
                        qualityPreset = static_cast<int>(ci);
                        break;
                    }
                }
                m_paramValues[p.key] = static_cast<double>(qualityPreset);
            }
            if (p.key == "cmaa2ExtraSharpness") {
                bool extraSharp      = pConfig->getOption<int32_t>(p.key, 0) != 0;
                m_paramValues[p.key] = extraSharp ? 1.0 : 0.0;
            }
        }

        // edgeThreshold isn't user facing, set directly from preset index
        if (qualityPreset == 0)
            specDataLocal.edgeThreshold = 0.15f;
        else if (qualityPreset == 1)
            specDataLocal.edgeThreshold = 0.10f;
        else if (qualityPreset == 2)
            specDataLocal.edgeThreshold = 0.07f;
        else
            specDataLocal.edgeThreshold = 0.05f;

        // Stage 2: Apply preset to config (if changed)
        const auto& presetTable = getPresetTable();
        auto presetIt           = presetTable.find(presetStr);

        const std::string appliedPresetKey = "cmaa2PresetApplied";
        const auto lastAppliedPreset       = pConfig->getOption<std::string>(appliedPresetKey, "");

        if (presetStr != lastAppliedPreset) {
            Logger::debug("Applying CMAA2 preset baseline: " + presetStr);
            const auto& params = getParamDescs();
            for (const auto& p : params) {
                if (p.key == "cmaa2QualityPreset" || p.key == "cmaa2ExtraSharpness") continue;

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
        const auto& params = getParamDescs();
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 2);

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
            } else if (p.type == ParamType::Bool || p.type == ParamType::Int) {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(p.defaultVal)));
            } else {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(p.defaultVal)));
            }
            val                  = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                auto f = static_cast<float>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specDataLocal) + p.specOffset, &f, sizeof(float));
            } else {
                auto i = static_cast<int32_t>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specDataLocal) + p.specOffset, &i, sizeof(int32_t));
            }
            mapEntries.push_back(
                {.constantID = static_cast<uint32_t>(p.specId),
                 .offset     = static_cast<uint32_t>(p.specOffset),
                 .size       = p.specSize});
        }

        // extraSharpness has no specId, so it isn't written to specDataLocal by the loop.
        // We read it from m_paramValues (populated in Stage 1) and apply derived values.
        bool extraSharpness                   = m_paramValues["cmaa2ExtraSharpness"] > 0.5;
        specDataLocal.localContrastAdaptation = extraSharpness ? 0.15f : 0.10f;
        specDataLocal.simpleShapeBluriness    = extraSharpness ? 0.07f : 0.10f;
        specDataLocal.extraSharpness          = extraSharpness ? 1 : 0;
        specDataLocal.colorSpaceMode          = static_cast<int32_t>(getColorSpaceMode(unormFormat, colorSpace));
        mapEntries.push_back(
            {.constantID = 65535, .offset = offsetof(Cmaa2SpecData, colorSpaceMode), .size = sizeof(int32_t)});

        VkFormat storageFormat = convertToUNORM(unormFormat);
        if (isFloatFormat(unormFormat)) {
            applyVariant                = DeferredApplyVariant::RGBA16F;
            specDataLocal.formatVariant = 2;
        } else if (is10BitPackedFormat(unormFormat)) {
            applyVariant                = DeferredApplyVariant::RGB10A2;
            specDataLocal.formatVariant = 1;
        } else {
            applyVariant                = DeferredApplyVariant::RGBA8;
            specDataLocal.formatVariant = 0;
        }

        mapEntries.push_back(
            {.constantID = 0, .offset = offsetof(Cmaa2SpecData, edgeThreshold), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 1, .offset = offsetof(Cmaa2SpecData, localContrastAdaptation), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 2, .offset = offsetof(Cmaa2SpecData, simpleShapeBluriness), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 3, .offset = offsetof(Cmaa2SpecData, formatVariant), .size = sizeof(int32_t)});
        mapEntries.push_back(
            {.constantID = 6, .offset = offsetof(Cmaa2SpecData, extraSharpness), .size = sizeof(int32_t)});
        mapEntries.push_back({.constantID = 7, .offset = offsetof(Cmaa2SpecData, resX), .size = sizeof(uint32_t)});
        mapEntries.push_back({.constantID = 8, .offset = offsetof(Cmaa2SpecData, resY), .size = sizeof(uint32_t)});
        mapEntries.push_back(
            {.constantID = 9, .offset = offsetof(Cmaa2SpecData, maxLineLength), .size = sizeof(uint32_t)});
        mapEntries.push_back(
            {.constantID = 10, .offset = offsetof(Cmaa2SpecData, minShapeLength), .size = sizeof(float)});
        mapEntries.push_back({.constantID = 11, .offset = offsetof(Cmaa2SpecData, armRatio), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 12, .offset = offsetof(Cmaa2SpecData, edgeDetectionMode), .size = sizeof(int32_t)});

        specDataLocal.resX = resX;
        specDataLocal.resY = resY;
        specData           = specDataLocal;

        inputImageViews = createImageViews(pLogicalDevice, unormFormat, inputImages);
        sampler         = createSampler(pLogicalDevice);

        outputStorageViews.resize(outputImages.size());
        for (size_t i = 0; i < outputImages.size(); i++) {
            outputStorageViews[i] = createImageViewHelper(pLogicalDevice, outputImages[i], storageFormat);
        }

        // Create 1x1 dummy images for the two unused output format variant bindings.
        const VkFormat dummyFormats[3] = {
            VK_FORMAT_R8G8B8A8_UNORM,           // binding 9
            VK_FORMAT_A2B10G10R10_UNORM_PACK32, // binding 10
            VK_FORMAT_R16G16B16A16_SFLOAT       // binding 11
        };
        for (int i = 0; i < 3; i++) {
            dummyOutputImages[i] = createDeviceLocalImage(
                pLogicalDevice, 1, 1, dummyFormats[i], VK_IMAGE_USAGE_STORAGE_BIT, dummyOutputMemory[i]);
            dummyOutputViews[i] = createImageViewHelper(pLogicalDevice, dummyOutputImages[i], dummyFormats[i]);
        }

        // Half width edge texture packs two 4 bit edge masks into one 8 bit R8_UINT texel
        uint32_t edgeResX = (resX + 1) / 2;
        edgeImage         = createDeviceLocalImage(
            pLogicalDevice, edgeResX, resY, VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, edgeMemory);
        edgeImageView = createImageViewHelper(pLogicalDevice, edgeImage, VK_FORMAT_R8_UINT);

        deferredBlendHeadsImage = createDeviceLocalImage(
            pLogicalDevice, (resX + 1) / 2, (resY + 1) / 2, VK_FORMAT_R32_UINT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, deferredBlendHeadsMemory);
        deferredBlendHeadsImageView =
            createImageViewHelper(pLogicalDevice, deferredBlendHeadsImage, VK_FORMAT_R32_UINT);

        const uint32_t requiredCandidatePixels    = resX * resY / 4;
        const uint32_t requiredDeferredColorApply = resX * resY / 2;
        const uint32_t requiredListHeadsPixels    = (resX * resY + 3) / 6;

        shapeCandidatesBuffer = createDeviceLocalBuffer(
            pLogicalDevice, requiredCandidatePixels * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, shapeCandidatesMemory);

        deferredBlendItemListBuffer = createDeviceLocalBuffer(
            pLogicalDevice, requiredDeferredColorApply * sizeof(uint32_t) * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, deferredBlendItemListMemory);

        deferredBlendLocationListBuffer = createDeviceLocalBuffer(
            pLogicalDevice, requiredListHeadsPixels * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, deferredBlendLocationListMemory);

        controlBuffer = createDeviceLocalBuffer(
            pLogicalDevice, 16 * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, controlMemory);

        executeIndirectBuffer = createDeviceLocalBuffer(
            pLogicalDevice, 4 * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            executeIndirectMemory);

        std::vector<VkDescriptorSetLayoutBinding> computeBindings(11);
        computeBindings[0] = {
            .binding            = 0,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = &sampler};
        computeBindings[1] = {
            .binding            = 2,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[2] = {
            .binding            = 3,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[3] = {
            .binding            = 4,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[4] = {
            .binding            = 5,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[5] = {
            .binding            = 6,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[6] = {
            .binding            = 7,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[7] = {
            .binding            = 8,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[8] = {
            .binding            = 9,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[9] = {
            .binding            = 10,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};
        computeBindings[10] = {
            .binding            = 11,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr};

        VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {};
        computeLayoutInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        computeLayoutInfo.bindingCount                    = 11;
        computeLayoutInfo.pBindings                       = computeBindings.data();
        pLogicalDevice->vkd.CreateDescriptorSetLayout(
            pLogicalDevice->device, &computeLayoutInfo, nullptr, &computeDescriptorSetLayout);

        VkDescriptorPoolSize computePoolSizes[] = {
            {.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .descriptorCount = static_cast<uint32_t>(inputImages.size())},
            {.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             .descriptorCount = static_cast<uint32_t>(inputImages.size()) * 5},
            {.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount = static_cast<uint32_t>(inputImages.size()) * 5},
        };
        VkDescriptorPoolCreateInfo computePoolInfo = {};
        computePoolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        computePoolInfo.maxSets                    = static_cast<uint32_t>(inputImages.size());
        computePoolInfo.poolSizeCount              = 3;
        computePoolInfo.pPoolSizes                 = computePoolSizes;
        pLogicalDevice->vkd.CreateDescriptorPool(
            pLogicalDevice->device, &computePoolInfo, nullptr, &computeDescriptorPool);

        computeDescriptorSets.resize(inputImages.size());
        std::vector<VkDescriptorSetLayout> computeLayouts(inputImages.size(), computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo computeAllocInfo = {};
        computeAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        computeAllocInfo.descriptorPool              = computeDescriptorPool;
        computeAllocInfo.descriptorSetCount          = static_cast<uint32_t>(inputImages.size());
        computeAllocInfo.pSetLayouts                 = computeLayouts.data();
        pLogicalDevice->vkd.AllocateDescriptorSets(
            pLogicalDevice->device, &computeAllocInfo, computeDescriptorSets.data());

        for (size_t i = 0; i < inputImages.size(); i++) {
            VkDescriptorImageInfo inputInfo = {
                .sampler     = sampler,
                .imageView   = inputImageViews[i],
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo outputStorageInfo = {
                .sampler = VK_NULL_HANDLE, .imageView = outputStorageViews[i], .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo edgeInfo = {
                .sampler = VK_NULL_HANDLE, .imageView = edgeImageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo headsInfo = {
                .sampler     = VK_NULL_HANDLE,
                .imageView   = deferredBlendHeadsImageView,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL};

            VkDescriptorBufferInfo shapeCandidatesInfo = {
                .buffer = shapeCandidatesBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkDescriptorBufferInfo blendLocationInfo = {
                .buffer = deferredBlendLocationListBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkDescriptorBufferInfo blendItemInfo = {
                .buffer = deferredBlendItemListBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkDescriptorBufferInfo controlInfo  = {.buffer = controlBuffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkDescriptorBufferInfo indirectInfo = {
                .buffer = executeIndirectBuffer, .offset = 0, .range = VK_WHOLE_SIZE};

            uint32_t realOutputBinding = 9 + static_cast<uint32_t>(applyVariant);

            // Build image info for all three output bindings. The active binding gets the real swapchain storage view, the other two are dummy views.
            VkDescriptorImageInfo outputImageInfos[3];
            for (int v = 0; v < 3; v++) {
                uint32_t binding = 9 + v;
                if (binding == realOutputBinding) {
                    outputImageInfos[v] = outputStorageInfo;
                } else {
                    outputImageInfos[v] = {
                        .sampler     = VK_NULL_HANDLE,
                        .imageView   = dummyOutputViews[v],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
                }
            }

            VkWriteDescriptorSet writes[11] = {
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 0,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .pImageInfo       = &inputInfo,
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 2,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo       = &edgeInfo,
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 3,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo       = nullptr,
                 .pBufferInfo      = &shapeCandidatesInfo,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 4,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo       = nullptr,
                 .pBufferInfo      = &blendLocationInfo,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 5,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo       = nullptr,
                 .pBufferInfo      = &blendItemInfo,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 6,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo       = &headsInfo,
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 7,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo       = nullptr,
                 .pBufferInfo      = &controlInfo,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 8,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .pImageInfo       = nullptr,
                 .pBufferInfo      = &indirectInfo,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 9,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo       = &outputImageInfos[0],
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 10,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo       = &outputImageInfos[1],
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr},
                {.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .pNext            = nullptr,
                 .dstSet           = computeDescriptorSets[i],
                 .dstBinding       = 11,
                 .dstArrayElement  = 0,
                 .descriptorCount  = 1,
                 .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 .pImageInfo       = &outputImageInfos[2],
                 .pBufferInfo      = nullptr,
                 .pTexelBufferView = nullptr}};
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 11, writes, 0, nullptr);
        }

        createShaderModule(pLogicalDevice, cmaa2_edges_comp, &edgesModule);
        createShaderModule(pLogicalDevice, cmaa2_dispatch_args_comp, &dispatchArgsModule);
        createShaderModule(pLogicalDevice, cmaa2_process_candidates_comp, &processCandidatesModule);
        createShaderModule(pLogicalDevice, cmaa2_deferred_apply_comp, &deferredApplyModule);
        createShaderModule(pLogicalDevice, cmaa2_debug_edges_comp, &debugEdgesModule);

        VkPipelineLayoutCreateInfo plInfo = {};
        plInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount             = 1;
        plInfo.pSetLayouts                = &computeDescriptorSetLayout;
        plInfo.pushConstantRangeCount     = 0;
        plInfo.pPushConstantRanges        = nullptr;
        pLogicalDevice->vkd.CreatePipelineLayout(pLogicalDevice->device, &plInfo, nullptr, &computePipelineLayout);

        VkSpecializationInfo specInfo = {};
        specInfo.mapEntryCount        = static_cast<uint32_t>(mapEntries.size());
        specInfo.pMapEntries          = mapEntries.data();
        specInfo.dataSize             = sizeof(Cmaa2SpecData);
        specInfo.pData                = &specData;

        auto createComputePipeline = [&](VkShaderModule module, VkPipeline& pipeline) {
            VkComputePipelineCreateInfo cpInfo = {};
            cpInfo.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpInfo.stage.sType                 = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module                = module;
            cpInfo.stage.pName                 = "main";
            cpInfo.stage.pSpecializationInfo   = &specInfo;
            cpInfo.layout                      = computePipelineLayout;
            pLogicalDevice->vkd.CreateComputePipelines(
                pLogicalDevice->device, pLogicalDevice->pipelineCache, 1, &cpInfo, nullptr, &pipeline);
        };

        createComputePipeline(edgesModule, edgesPipeline);
        createComputePipeline(dispatchArgsModule, dispatchArgsPipeline);
        createComputePipeline(processCandidatesModule, processCandidatesPipeline);
        createComputePipeline(deferredApplyModule, deferredApplyPipeline);
        createComputePipeline(debugEdgesModule, debugEdgesPipeline);

        // One time GPU setup: transition working images to GENERAL and zero the control buffers.
        {
            VkCommandBufferAllocateInfo cmdAllocInfo = {};
            cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool                 = pLogicalDevice->commandPool;
            cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount          = 1;

            VkCommandBuffer setupCmd = VK_NULL_HANDLE;
            pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &cmdAllocInfo, &setupCmd);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            pLogicalDevice->vkd.BeginCommandBuffer(setupCmd, &beginInfo);

            VkImageMemoryBarrier imageBarriers[5] = {};
            imageBarriers[0].sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[0].srcAccessMask        = 0;
            imageBarriers[0].dstAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
            imageBarriers[0].oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarriers[0].newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            imageBarriers[0].image                = edgeImage;
            imageBarriers[0].subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            imageBarriers[1].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[1].srcAccessMask    = 0;
            imageBarriers[1].dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
            imageBarriers[1].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarriers[1].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            imageBarriers[1].image            = deferredBlendHeadsImage;
            imageBarriers[1].subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            for (int i = 0; i < 3; i++) {
                imageBarriers[2 + i].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageBarriers[2 + i].srcAccessMask    = 0;
                imageBarriers[2 + i].dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                imageBarriers[2 + i].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                imageBarriers[2 + i].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
                imageBarriers[2 + i].image            = dummyOutputImages[i];
                imageBarriers[2 + i].subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1};
            }

            pLogicalDevice->vkd.CmdPipelineBarrier(
                setupCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                nullptr, 5, imageBarriers);

            pLogicalDevice->vkd.CmdFillBuffer(setupCmd, controlBuffer, 0, VK_WHOLE_SIZE, 0);
            pLogicalDevice->vkd.CmdFillBuffer(setupCmd, executeIndirectBuffer, 0, VK_WHOLE_SIZE, 0);

            VkBufferMemoryBarrier zeroBarriers[2] = {};
            zeroBarriers[0].sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            zeroBarriers[0].srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
            zeroBarriers[0].dstAccessMask         = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            zeroBarriers[0].buffer                = controlBuffer;
            zeroBarriers[0].size                  = VK_WHOLE_SIZE;

            zeroBarriers[1].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            zeroBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            zeroBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            zeroBarriers[1].buffer        = executeIndirectBuffer;
            zeroBarriers[1].size          = VK_WHOLE_SIZE;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                setupCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2,
                zeroBarriers, 0, nullptr);

            pLogicalDevice->vkd.EndCommandBuffer(setupCmd);

            VkFenceCreateInfo fenceInfo = {};
            fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence setupFence          = VK_NULL_HANDLE;
            pLogicalDevice->vkd.CreateFence(pLogicalDevice->device, &fenceInfo, nullptr, &setupFence);

            VkSubmitInfo submitInfo       = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &setupCmd;
            pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, setupFence);

            pLogicalDevice->vkd.WaitForFences(pLogicalDevice->device, 1, &setupFence, VK_TRUE, UINT64_MAX);

            pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, setupFence, nullptr);
            pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool, 1, &setupCmd);
        }
    }

    void Cmaa2Effect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        const uint32_t resX = imageExtent.width;
        const uint32_t resY = imageExtent.height;

        VkImage targetImage = m_isInPlace ? inputImages[imageIndex] : outputImages[imageIndex];

        if (m_isInPlace) {
            // In-place: single barrier to SHADER_READ_ONLY for passes 1-3
            VkImageMemoryBarrier readBarrier = {};
            readBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            readBarrier.srcAccessMask =
                isFirstInChain ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
            readBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            readBarrier.oldLayout =
                isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            readBarrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            readBarrier.image            = targetImage;
            readBarrier.subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer,
                isFirstInChain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                               : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &readBarrier);
        } else {
            // Ping-pong: copy input to output, then transition both
            VkImage storageTarget = outputImages[imageIndex];

            VkImageMemoryBarrier inputToTransferBarrier = {};
            inputToTransferBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            inputToTransferBarrier.srcAccessMask =
                isFirstInChain ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
            inputToTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            inputToTransferBarrier.oldLayout =
                isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            inputToTransferBarrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            inputToTransferBarrier.image            = inputImages[imageIndex];
            inputToTransferBarrier.subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer,
                isFirstInChain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                               : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &inputToTransferBarrier);

            VkImageMemoryBarrier targetToTransferBarrier = {};
            targetToTransferBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            targetToTransferBarrier.srcAccessMask        = 0;
            targetToTransferBarrier.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
            targetToTransferBarrier.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
            targetToTransferBarrier.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            targetToTransferBarrier.image                = storageTarget;
            targetToTransferBarrier.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                nullptr, 1, &targetToTransferBarrier);

            VkImageCopy copyRegion    = {};
            copyRegion.srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
            copyRegion.srcOffset      = {.x = 0, .y = 0, .z = 0};
            copyRegion.dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
            copyRegion.dstOffset = {.x = 0, .y = 0, .z = 0};
            copyRegion.extent    = {.width = resX, .height = resY, .depth = 1};
            pLogicalDevice->vkd.CmdCopyImage(
                commandBuffer, inputImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, storageTarget,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            VkImageMemoryBarrier inputToShaderBarrier = {};
            inputToShaderBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            inputToShaderBarrier.srcAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
            inputToShaderBarrier.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
            inputToShaderBarrier.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            inputToShaderBarrier.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            inputToShaderBarrier.image                = inputImages[imageIndex];
            inputToShaderBarrier.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                nullptr, 1, &inputToShaderBarrier);

            VkImageMemoryBarrier targetToGeneralBarrier = {};
            targetToGeneralBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            targetToGeneralBarrier.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
            targetToGeneralBarrier.dstAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
            targetToGeneralBarrier.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            targetToGeneralBarrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            targetToGeneralBarrier.image                = storageTarget;
            targetToGeneralBarrier.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                nullptr, 1, &targetToGeneralBarrier);
        }

        {
            uint32_t threadGroupCountX = (resX + 27) / 28;
            uint32_t threadGroupCountY = (resY + 27) / 28;
            pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, edgesPipeline);
            pLogicalDevice->vkd.CmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1,
                &computeDescriptorSets[imageIndex], 0, nullptr);
            pLogicalDevice->vkd.CmdDispatch(commandBuffer, threadGroupCountX, threadGroupCountY, 1);
        }

        VkMemoryBarrier memBarrier1 = {};
        memBarrier1.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier1.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier1.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
            &memBarrier1, 0, nullptr, 0, nullptr);

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dispatchArgsPipeline);
        pLogicalDevice->vkd.CmdDispatch(commandBuffer, 2, 1, 1);

        VkBufferMemoryBarrier indirectBarrier1 = {};
        indirectBarrier1.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        indirectBarrier1.srcAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier1.dstAccessMask         = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier1.buffer                = executeIndirectBuffer;
        indirectBarrier1.size                  = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1,
            &indirectBarrier1, 0, nullptr);

        VkBufferMemoryBarrier controlBarrier1 = {};
        controlBarrier1.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        controlBarrier1.srcAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
        controlBarrier1.dstAccessMask         = VK_ACCESS_SHADER_READ_BIT;
        controlBarrier1.buffer                = controlBuffer;
        controlBarrier1.size                  = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
            &controlBarrier1, 0, nullptr);

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, processCandidatesPipeline);
        pLogicalDevice->vkd.CmdDispatchIndirect(commandBuffer, executeIndirectBuffer, 0);

        VkBufferMemoryBarrier indirectBarrier2 = {};
        indirectBarrier2.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        indirectBarrier2.srcAccessMask         = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier2.dstAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier2.buffer                = executeIndirectBuffer;
        indirectBarrier2.size                  = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
            &indirectBarrier2, 0, nullptr);

        VkMemoryBarrier memBarrier2 = {};
        memBarrier2.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier2.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier2.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
            &memBarrier2, 0, nullptr, 0, nullptr);

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dispatchArgsPipeline);
        pLogicalDevice->vkd.CmdDispatch(commandBuffer, 1, 2, 1);

        VkBufferMemoryBarrier indirectBarrier3 = {};
        indirectBarrier3.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        indirectBarrier3.srcAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier3.dstAccessMask         = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier3.buffer                = executeIndirectBuffer;
        indirectBarrier3.size                  = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1,
            &indirectBarrier3, 0, nullptr);

        VkBufferMemoryBarrier controlBarrier2 = {};
        controlBarrier2.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        controlBarrier2.srcAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
        controlBarrier2.dstAccessMask         = VK_ACCESS_SHADER_READ_BIT;
        controlBarrier2.buffer                = controlBuffer;
        controlBarrier2.size                  = VK_WHOLE_SIZE;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
            &controlBarrier2, 0, nullptr);

        if (m_isInPlace) {
            // Transition from SHADER_READ_ONLY to GENERAL for the deferred apply writeback
            VkImageMemoryBarrier writeBarrier = {};
            writeBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            writeBarrier.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
            writeBarrier.dstAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
            writeBarrier.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writeBarrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            writeBarrier.image                = targetImage;
            writeBarrier.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                nullptr, 0, nullptr, 1, &writeBarrier);
        }

        if (specData.debugEdges != 0) {
            uint32_t tgcX = (resX + 15) / 16;
            uint32_t tgcY = (resY + 15) / 16;

            pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, debugEdgesPipeline);
            pLogicalDevice->vkd.CmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1,
                &computeDescriptorSets[imageIndex], 0, nullptr);
            pLogicalDevice->vkd.CmdDispatch(commandBuffer, tgcX, tgcY, 1);

            VkMemoryBarrier debugBarrier = {};
            debugBarrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            debugBarrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
            debugBarrier.dstAccessMask   = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                &debugBarrier, 0, nullptr, 0, nullptr);
        }

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredApplyPipeline);
        pLogicalDevice->vkd.CmdDispatchIndirect(commandBuffer, executeIndirectBuffer, 0);

        VkImageMemoryBarrier outputBarrier = {};
        outputBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        outputBarrier.srcAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
        outputBarrier.dstAccessMask        = isLastInChain ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
        outputBarrier.oldLayout            = VK_IMAGE_LAYOUT_GENERAL;
        outputBarrier.newLayout =
            isLastInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outputBarrier.image            = targetImage; // Uses targetImage to handle both in-place and ping-pong
        outputBarrier.subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            isLastInChain ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                          : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
            0, 0, nullptr, 0, nullptr, 1, &outputBarrier);
    }

    const std::vector<EffectParamDesc>& Cmaa2Effect::getParamDescs() const
    {
        static const std::vector<EffectParamDesc> params = {
            {.key          = "cmaa2QualityPreset",
             .label        = "Quality Preset",
             .type         = ParamType::Combo,
             .defaultVal   = 3.0,
             .minVal       = 0.0,
             .maxVal       = 3.0,
             .step         = 1.0,
             .comboOptions = {"Low", "Medium", "High", "Ultra"},
             .category     = "Anti-Aliasing",
             .tooltip = "CMAA2 quality preset. Controls edge threshold, max trace length, min shape length, and arm "
                        "asymmetry.\n"
                        "Low: 0.15 thresh, 32 max len, 8.0 min len, 1.10 ratio (fastest)\n"
                        "Medium: 0.10 thresh, 64 max len, 6.0 min len, 1.20 ratio\n"
                        "High: 0.07 thresh, 86 max len, 5.0 min len, 1.25 ratio (Intel reference)\n"
                        "Ultra: 0.05 thresh, 128 max len, 3.0 min len, 2.00 ratio (default, still conservative but "
                        "good detection)"},

            {.key          = "cmaa2EdgeDetectionMode",
             .label        = "Edge Detection Mode",
             .type         = ParamType::Combo,
             .defaultVal   = 0.0,
             .minVal       = 0.0,
             .maxVal       = 1.0,
             .step         = 1.0,
             .comboOptions = {"Luminance (Fast)", "Color (High Quality)"},
             .category     = "Anti-Aliasing",
             .tooltip =
                 "Luminance is faster and standard (intel rec). Color uses full RGB channels for edge detection,\n"
                 "which can catch subtle color transitions (e.g. dark red next to dark blue) that luma\n"
                 "misses, at a slight performance cost.",
             SPEC(12, edgeDetectionMode)},

            {.key        = "cmaa2ExtraSharpness",
             .label      = "Extra Sharpness",
             .type       = ParamType::Bool,
             .defaultVal = 0.0,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 1.0,
             .category   = "Anti-Aliasing",
             .tooltip    = "Preserve more text and shape clarity at the expense of slightly less anti-aliasing.\n"
                           "Increases local contrast adaptation and reduces simple shape blur."},

            {.key        = "cmaa2MaxLineLength",
             .label      = "Max Line Length",
             .type       = ParamType::Int,
             .defaultVal = 128.0,
             .minVal     = 32.0,
             .maxVal     = 128.0,
             .step       = 2.0,
             .category   = "Anti-Aliasing",
             .tooltip    = "Maximum Z-shape arm tracing distance.\n"
                           "Higher values trace longer diagonal lines but cost more.\n"
                           "32 = fast/low quality, 86 = Intel default, 128 = maximum (Ultra default).",
             SPEC(9, maxLineLength)},

            {.key        = "cmaa2MinShapeLength",
             .label      = "Min Shape Length",
             .type       = ParamType::Float,
             .defaultVal = 3.0,
             .minVal     = 3.0,
             .maxVal     = 8.0,
             .step       = 0.5,
             .category   = "Anti-Aliasing",
             .tooltip    = "Minimum combined Z-shape arm length to qualify for anti-aliasing.\n"
                           "Lower values catch shorter diagonals but may introduce artifacts.\n"
                           "3.0 = aggressive (Ultra default), 5.0 = Intel default, 8.0 = conservative.",
             SPEC(10, minShapeLength)},

            {.key        = "cmaa2ArmRatio",
             .label      = "Arm Ratio",
             .type       = ParamType::Float,
             .defaultVal = 2.00,
             .minVal     = 1.10,
             .maxVal     = 2.00,
             .step       = 0.05,
             .category   = "Anti-Aliasing",
             .tooltip    = "Maximum allowed ratio between the longer and shorter Z-shape arm.\n"
                           "Higher values allow more asymmetric shapes (catches shallow diagonals).\n"
                           "1.10 = very strict, 1.25 = Intel default, 2.0 = permissive (Ultra default).\n"
                           "Extra Sharpness mode caps this at 1.20 regardless of this setting.",
             SPEC(11, armRatio)},

            {.key        = "cmaa2DebugAA",
             .label      = "Debug AA",
             .type       = ParamType::Bool,
             .defaultVal = 0.0,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 1.0,
             .category   = "Debug",
             .tooltip = "Visualizes CMAA2 activity as a cyan heat overlay. Brighter cyan = more anti-aliasing applied "
                        "to that pixel.",
             SPEC(4, debugAA)},

            {.key        = "cmaa2DebugEdges",
             .label      = "Debug Edges",
             .type       = ParamType::Bool,
             .defaultVal = 0.0,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 1.0,
             .category   = "Debug",
             .tooltip =
                 "Visualizes detected edges using a thermal heatmap (intensity) and directional tints (orientation).\n"
                 "Heatmap: Dark (0) -> Purple (1) -> Red (2) -> Orange (3) -> White-hot (4 edges).\n"
                 "Tints: Cyan (Right), Green (Bottom), Blue (Left), White (Top).\n"
                 "Useful for understanding what the edge detection pass sees before shape tracing.",
             SPEC(5, debugEdges)},
        };
        return params;
    }

    Cmaa2Effect::~Cmaa2Effect()
    {
        Logger::debug("destroying Cmaa2Effect");

        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, edgesPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, dispatchArgsPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, processCandidatesPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, deferredApplyPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, debugEdgesPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, computePipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, computeDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, edgesModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, dispatchArgsModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, processCandidatesModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, deferredApplyModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, debugEdgesModule, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, computeDescriptorPool, nullptr);

        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, edgeImageView, nullptr);
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, deferredBlendHeadsImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, edgeImage, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, deferredBlendHeadsImage, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, edgeMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, deferredBlendHeadsMemory, nullptr);

        for (auto& outputStorageView : outputStorageViews)
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputStorageView, nullptr);

        for (int i = 0; i < 3; i++) {
            if (dummyOutputViews[i] != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, dummyOutputViews[i], nullptr);
            if (dummyOutputImages[i] != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, dummyOutputImages[i], nullptr);
            if (dummyOutputMemory[i] != VK_NULL_HANDLE)
                pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, dummyOutputMemory[i], nullptr);
        }

        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, shapeCandidatesBuffer, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, deferredBlendItemListBuffer, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, deferredBlendLocationListBuffer, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, controlBuffer, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, executeIndirectBuffer, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, shapeCandidatesMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, deferredBlendItemListMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, deferredBlendLocationListMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, controlMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, executeIndirectMemory, nullptr);

        for (size_t i = 0; i < inputImageViews.size(); i++) {
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
        }
        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
    }

} // namespace vkBasalt
