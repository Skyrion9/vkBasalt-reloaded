#include "effect_deband.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "effect.hpp"
#include "effect_simple.hpp"
#include "logical_device.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    struct DebandSpecData {
        float   screenWidth;
        float   screenHeight;
        float   reverseScreenWidth;
        float   reverseScreenHeight;
        float   debandAvgdiff;
        float   debandMaxdiff;
        float   debandMiddiff;
        float   range;
        int32_t iterations;
        int32_t colorSpaceMode;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(DebandSpecData, field), .specSize = sizeof(((DebandSpecData*)0)->field)

    DebandEffect::DebandEffect(LogicalDevice*       pLogicalDevice,
                               VkFormat             format,
                               VkExtent2D           imageExtent,
                               std::vector<VkImage> inputImages,
                               std::vector<VkImage> outputImages,
                               Config*              pConfig,
                               VkColorSpaceKHR      colorSpace)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = deband_frag;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params = getParamDescs();
        DebandSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 5);

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val;
            if (p.type == ParamType::Combo) {
                std::string strVal = pConfig->getOption<std::string>(p.key, "");
                int idx = 0;
                for (size_t ci = 0; ci < p.comboOptions.size(); ci++) {
                    if (p.comboOptions[ci] == strVal) { idx = (int)ci; break; }
                }
                val = (double)idx;
            } else if (p.type == ParamType::Float) {
                val = (double)pConfig->getOption<float>(p.key, (float)p.defaultVal);
            } else {
                val = (double)pConfig->getOption<int32_t>(p.key, (int32_t)p.defaultVal);
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

        specData.screenWidth         = (float)imageExtent.width;
        specData.screenHeight        = (float)imageExtent.height;
        specData.reverseScreenWidth  = 1.0f / imageExtent.width;
        specData.reverseScreenHeight = 1.0f / imageExtent.height;
        specData.colorSpaceMode      = static_cast<int32_t>(csm);

        mapEntries.push_back({0, offsetof(DebandSpecData, screenWidth),         sizeof(float)});
        mapEntries.push_back({1, offsetof(DebandSpecData, screenHeight),        sizeof(float)});
        mapEntries.push_back({2, offsetof(DebandSpecData, reverseScreenWidth),  sizeof(float)});
        mapEntries.push_back({3, offsetof(DebandSpecData, reverseScreenHeight), sizeof(float)});
        mapEntries.push_back({65535, offsetof(DebandSpecData, colorSpaceMode),  sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(DebandSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    DebandEffect::~DebandEffect() {}

    const std::vector<EffectParamDesc>& DebandEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "debandAvgdiff", .label = "Avg Diff Threshold", .type = ParamType::Float,
             .defaultVal = 3.4, .minVal = 0.0, .maxVal = 20.0, .step = 0.1,
             .category = "Debanding",
             .tooltip = "Average color difference threshold. Neighbors within this range are considered part of the same band. Lower = more aggressive debanding. Default 3.4.",
             SPEC(4, debandAvgdiff)},

            {.key = "debandMaxdiff", .label = "Max Diff Threshold", .type = ParamType::Float,
             .defaultVal = 6.8, .minVal = 0.0, .maxVal = 40.0, .step = 0.1,
             .category = "Debanding",
             .tooltip = "Maximum allowed difference between any single neighbor. Prevents debanding from bleeding across strong edges. Higher = less protection. Default 6.8.",
             SPEC(5, debandMaxdiff)},

            {.key = "debandMiddiff", .label = "Mid Diff Threshold", .type = ParamType::Float,
             .defaultVal = 3.3, .minVal = 0.0, .maxVal = 20.0, .step = 0.1,
             .category = "Debanding",
             .tooltip = "Median neighbor difference threshold. Works with Avg Diff to classify flat banding regions. Lower = more aggressive. Default 3.3.",
             SPEC(6, debandMiddiff)},

            {.key = "debandRange", .label = "Range", .type = ParamType::Float,
             .defaultVal = 16.0, .minVal = 1.0, .maxVal = 64.0, .step = 1.0,
             .category = "Debanding",
             .tooltip = "Sampling radius in pixels. Larger range catches wider banding gradients but costs more. Default 16.0.",
             SPEC(7, range)},

            {.key = "debandIterations", .label = "Iterations", .type = ParamType::Int,
             .defaultVal = 4.0, .minVal = 1.0, .maxVal = 8.0, .step = 1.0,
             .category = "Debanding",
             .tooltip = "Number of sampling iterations per pixel (shader loop bound). Higher = smoother result, more GPU cost. Default 4.",
             SPEC(8, iterations)},
        };
        return params;
    }
} // namespace vkBasalt
