#include "effect_dls.hpp"

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
    struct DlsSpecData {
        float sharpen;
        float denoise;
        int32_t colorSpaceMode;
    };

    #define SPEC(id, field) .specId = id, .specOffset = offsetof(DlsSpecData, field), .specSize = sizeof(((DlsSpecData*)0)->field)

    DlsEffect::DlsEffect(LogicalDevice*       pLogicalDevice,
                         VkFormat             format,
                         VkExtent2D           imageExtent,
                         std::vector<VkImage> inputImages,
                         std::vector<VkImage> outputImages,
                         Config*              pConfig,
                         VkColorSpaceKHR      colorSpace)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = dls_frag;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params = getParamDescs();
        DlsSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 1);

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

        specData.colorSpaceMode = static_cast<int32_t>(csm);
        mapEntries.push_back({65535, offsetof(DlsSpecData, colorSpaceMode), sizeof(int32_t)});

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        specializationInfo.pMapEntries   = mapEntries.data();
        specializationInfo.dataSize      = sizeof(DlsSpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    DlsEffect::~DlsEffect() {}

    const std::vector<EffectParamDesc>& DlsEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "dlsSharpness", .label = "Sharpness", .type = ParamType::Float,
             .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Sharpening",
             .tooltip = "Luma sharpening strength. Enhances local contrast via unsharp mask. Higher = more visible sharpening. Default 0.5.",
             SPEC(0, sharpen)},

            {.key = "dlsDenoise", .label = "Denoise", .type = ParamType::Float,
             .defaultVal = 0.17, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Denoising",
             .tooltip = "Denoising strength. Blends pixel toward local average to reduce compression noise and film grain. Higher = smoother but softer. Default 0.17.",
             SPEC(1, denoise)},
        };
        return params;
    }
} // namespace vkBasalt
