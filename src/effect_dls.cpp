#include "effect_dls.hpp"

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
    .specId = (id), .specOffset = offsetof(DlsSpecData, field), .specSize = sizeof(((DlsSpecData*) 0)->field)

    DlsEffect::DlsEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace)
    {
        vertexCode   = decompressShaderCached(full_screen_triangle_vert);
        fragmentCode = decompressShaderCached(dls_frag);

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params   = getParamDescs();
        DlsSpecData specData = {};
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
            {.constantID = 65535, .offset = offsetof(DlsSpecData, colorSpaceMode), .size = sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(DlsSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, std::move(inputImages), std::move(outputImages), pConfig);
    }

    DlsEffect::~DlsEffect() = default;

    const std::vector<EffectParamDesc>& DlsEffect::getParamDescs() const
    {
        static const std::vector<EffectParamDesc> params = {
            {.key        = "dlsSharpness",
             .label      = "Sharpness",
             .type       = ParamType::Float,
             .defaultVal = 0.5,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 0.01,
             .category   = "Sharpening",
             .tooltip    = "Luma sharpening strength. Enhances local contrast via unsharp mask. Higher = more visible "
                           "sharpening. Default 0.5.",
             SPEC(0, sharpen)},

            {.key        = "dlsDenoise",
             .label      = "Denoise",
             .type       = ParamType::Float,
             .defaultVal = 0.17,
             .minVal     = 0.0,
             .maxVal     = 1.0,
             .step       = 0.01,
             .category   = "Denoising",
             .tooltip    = "Denoising strength. Blends pixel toward local average to reduce compression noise and film "
                           "grain. Higher = smoother but softer. Default 0.17.",
             SPEC(1, denoise)},
        };
        return params;
    }
} // namespace vkBasalt
