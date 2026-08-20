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
        int32_t hdrMode;
    };

    #define SPEC(id, field) id, offsetof(DebandSpecData, field), sizeof(((DebandSpecData*)0)->field)

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

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        const auto& params = getParamDescs();
        DebandSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

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
        specData.hdrMode             = isHDR ? 1 : 0;

        mapEntries.push_back({0, offsetof(DebandSpecData, screenWidth),         sizeof(float)});
        mapEntries.push_back({1, offsetof(DebandSpecData, screenHeight),        sizeof(float)});
        mapEntries.push_back({2, offsetof(DebandSpecData, reverseScreenWidth),  sizeof(float)});
        mapEntries.push_back({3, offsetof(DebandSpecData, reverseScreenHeight), sizeof(float)});
        mapEntries.push_back({9, offsetof(DebandSpecData, hdrMode),             sizeof(int32_t)});

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
            {"debandAvgdiff",    "Avg Diff Threshold", ParamType::Float, 3.4,  0.0, 20.0, 0.1, {}, "Debanding", SPEC(4, debandAvgdiff)},
            {"debandMaxdiff",    "Max Diff Threshold", ParamType::Float, 6.8,  0.0, 40.0, 0.1, {}, "Debanding", SPEC(5, debandMaxdiff)},
            {"debandMiddiff",    "Mid Diff Threshold", ParamType::Float, 3.3,  0.0, 20.0, 0.1, {}, "Debanding", SPEC(6, debandMiddiff)},
            {"debandRange",      "Range",              ParamType::Float, 16.0, 1.0, 64.0, 1.0, {}, "Debanding", SPEC(7, range)},
            {"debandIterations", "Iterations",         ParamType::Int,   4.0,  1.0,  8.0, 1.0, {}, "Debanding", SPEC(8, iterations)},
        };
        return params;
    }
} // namespace vkBasalt
