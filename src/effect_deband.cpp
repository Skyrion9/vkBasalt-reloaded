#include "effect_deband.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <math.h>
#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "effect.hpp"
#include "effect_simple.hpp"
#include "logical_device.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
#define SPEC(id, field) \
    .specId = (id), .specOffset = offsetof(DebandSpecData, field), .specSize = sizeof(((DebandSpecData*) 0)->field)

    DebandEffect::DebandEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace)
    {
        vertexCode             = decompressShaderCached(full_screen_triangle_vert);
        fragmentCode           = decompressShaderCached(deband_frag);
        this->pushConstantSize = 0;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params      = getParamDescs();
        DebandSpecData specData = {};
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

        specData.screenWidth         = static_cast<float>(imageExtent.width);
        specData.screenHeight        = static_cast<float>(imageExtent.height);
        specData.reverseScreenWidth  = 1.0f / imageExtent.width;
        specData.reverseScreenHeight = 1.0f / imageExtent.height;
        specData.colorSpaceMode      = static_cast<int32_t>(csm);

        mapEntries.push_back({.constantID = 0, .offset = offsetof(DebandSpecData, screenWidth), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 1, .offset = offsetof(DebandSpecData, screenHeight), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 2, .offset = offsetof(DebandSpecData, reverseScreenWidth), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 3, .offset = offsetof(DebandSpecData, reverseScreenHeight), .size = sizeof(float)});
        mapEntries.push_back(
            {.constantID = 65535, .offset = offsetof(DebandSpecData, colorSpaceMode), .size = sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(DebandSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, std::move(inputImages), std::move(outputImages), pConfig);
    }

    DebandEffect::~DebandEffect() = default;

    const std::vector<EffectParamDesc>& DebandEffect::getParamDescs() const
    {
        static const std::vector<EffectParamDesc> params = {
            {.key        = "debandAvgdiff",
             .label      = "Avg Diff Threshold",
             .type       = ParamType::Float,
             .defaultVal = 3.4,
             .minVal     = 0.0,
             .maxVal     = 20.0,
             .step       = 0.1,
             .category   = "Debanding",
             .tooltip    = "Average color difference threshold. Neighbors within this range are considered part of the "
                           "same band. Lower = more aggressive debanding. Default 3.4.",
             SPEC(4, debandAvgdiff)},

            {.key        = "debandMaxdiff",
             .label      = "Max Diff Threshold",
             .type       = ParamType::Float,
             .defaultVal = 6.8,
             .minVal     = 0.0,
             .maxVal     = 40.0,
             .step       = 0.1,
             .category   = "Debanding",
             .tooltip    = "Maximum allowed difference between any single neighbor. Prevents debanding from bleeding "
                           "across strong edges. Higher = less protection. Default 6.8.",
             SPEC(5, debandMaxdiff)},

            {.key        = "debandMiddiff",
             .label      = "Mid Diff Threshold",
             .type       = ParamType::Float,
             .defaultVal = 3.3,
             .minVal     = 0.0,
             .maxVal     = 20.0,
             .step       = 0.1,
             .category   = "Debanding",
             .tooltip = "Median neighbor difference threshold. Works with Avg Diff to classify flat banding regions. "
                        "Lower = more aggressive. Default 3.3.",
             SPEC(6, debandMiddiff)},

            {.key        = "debandRange",
             .label      = "Range",
             .type       = ParamType::Float,
             .defaultVal = 16.0,
             .minVal     = 1.0,
             .maxVal     = 64.0,
             .step       = 1.0,
             .category   = "Debanding",
             .tooltip    = "Sampling radius in pixels. Larger range catches wider banding gradients but costs more. "
                           "Default 16.0.",
             SPEC(7, range)},

            {.key        = "debandIterations",
             .label      = "Iterations",
             .type       = ParamType::Int,
             .defaultVal = 4.0,
             .minVal     = 1.0,
             .maxVal     = 8.0,
             .step       = 1.0,
             .category   = "Debanding",
             .tooltip = "Number of sampling iterations per pixel (shader loop bound). Higher = smoother result, more "
                        "GPU cost. Default 4.",
             SPEC(8, iterations)},
        };
        return params;
    }
} // namespace vkBasalt
