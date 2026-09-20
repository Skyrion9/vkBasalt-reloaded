#include "effect_cas.hpp"

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
#include "logical_device.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{

#define SPEC(id, field) \
    .specId = (id), .specOffset = offsetof(CasSpecData, field), .specSize = sizeof(((CasSpecData*) 0)->field)

    CasEffect::CasEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace)
    {
        vertexCode             = decompressShaderCached(full_screen_triangle_vert);
        fragmentCode           = decompressShaderCached(cas_frag);
        this->pushConstantSize = 0;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params   = getParamDescs();
        CasSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 1);

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val = NAN;
            if (p.type == ParamType::Float) {
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

        specData.colorSpaceMode = static_cast<int32_t>(csm);
        mapEntries.push_back(
            {.constantID = 65535, .offset = offsetof(CasSpecData, colorSpaceMode), .size = sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(CasSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, std::move(inputImages), std::move(outputImages), pConfig);
    }

    CasEffect::~CasEffect() = default;

    const std::vector<EffectParamDesc>& CasEffect::getParamDescs() const
    {
        static const std::vector<EffectParamDesc> params = {
            {.key        = "casSharpness",
             .label      = "Sharpness",
             .type       = ParamType::Float,
             .defaultVal = 0.4,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 0.01,
             .category   = "Sharpening",
             SPEC(0, sharpness)},

            {.key        = "casContrastLimit",
             .label      = "Contrast Limit",
             .type       = ParamType::Float,
             .defaultVal = 0.0,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 0.01,
             .category   = "Sharpening",
             SPEC(1, contrastLimit)},
        };
        return params;
    }
} // namespace vkBasalt
