#include "effect_cas.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "config.hpp"
#include "effect.hpp"
#include "logical_device.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    struct CasSpecData {
        float sharpness;
        float contrastLimit;
        int32_t hdrMode;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(CasSpecData, field), .specSize = sizeof(((CasSpecData*)0)->field)

    CasEffect::CasEffect(LogicalDevice*       pLogicalDevice,
                         VkFormat             format,
                         VkExtent2D           imageExtent,
                         std::vector<VkImage> inputImages,
                         std::vector<VkImage> outputImages,
                         Config*              pConfig,
                         VkColorSpaceKHR      colorSpace)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = cas_frag;

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        const auto& params = getParamDescs();
        CasSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size());

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val;
            if (p.type == ParamType::Float) {
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

        specData.hdrMode = isHDR ? 1 : 0;
        mapEntries.push_back({2, offsetof(CasSpecData, hdrMode), sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(CasSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    CasEffect::~CasEffect() {}

    const std::vector<EffectParamDesc>& CasEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "casSharpness", .label = "Sharpness", .type = ParamType::Float,
             .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Sharpening", SPEC(0, sharpness)},

            {.key = "casContrastLimit", .label = "Contrast Limit", .type = ParamType::Float,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Sharpening", SPEC(1, contrastLimit)},
        };
        return params;
    }
} // namespace vkBasalt
