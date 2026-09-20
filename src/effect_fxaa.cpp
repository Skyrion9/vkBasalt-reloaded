#include "effect_fxaa.hpp"

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
    #define SPEC(id, field) .specId = (id), .specOffset = offsetof(FxaaSpecData, field), .specSize = sizeof(((FxaaSpecData*)0)->field)

    FxaaEffect::FxaaEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig,
                           VkColorSpaceKHR      colorSpace)
    {
        vertexCode   = decompressShaderCached(full_screen_triangle_vert);
        fragmentCode = decompressShaderCached(fxaa_frag);

        // Prevent the pipeline layout from allocating a push constant range, tells SimpleEffect::applyEffect to skip the CmdPushConstants API call.
        this->pushConstantSize = 0;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        const auto& params = getParamDescs();
        FxaaSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 3);

        for (const auto& p : params) {
            if (p.specId < 0) continue;

            double val = NAN;
            if (p.type == ParamType::Float) {
                val = static_cast<double>(pConfig->getOption<float>(p.key, static_cast<float>(p.defaultVal)));
            } else {
                val = static_cast<double>(pConfig->getOption<int32_t>(p.key, static_cast<int32_t>(p.defaultVal)));
            }

            val = std::clamp(val, p.minVal, p.maxVal);
            m_paramValues[p.key] = val;

            if (p.type == ParamType::Float) {
                auto f = static_cast<float>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specData) + p.specOffset, &f, sizeof(float));
            } else {
                auto i = static_cast<int32_t>(val);
                std::memcpy(reinterpret_cast<uint8_t*>(&specData) + p.specOffset, &i, sizeof(int32_t));
            }

            mapEntries.push_back({.constantID=static_cast<uint32_t>(p.specId), .offset=static_cast<uint32_t>(p.specOffset), .size=p.specSize});
        }

        specData.screenWidth  = static_cast<float>(imageExtent.width);
        specData.screenHeight = static_cast<float>(imageExtent.height);
        specData.colorSpaceMode = static_cast<int32_t>(csm);

        mapEntries.push_back({.constantID=3, .offset=offsetof(FxaaSpecData, screenWidth),  .size=sizeof(float)});
        mapEntries.push_back({.constantID=4, .offset=offsetof(FxaaSpecData, screenHeight), .size=sizeof(float)});
        mapEntries.push_back({.constantID=65535, .offset=offsetof(FxaaSpecData, colorSpaceMode), .size=sizeof(int32_t)});

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = static_cast<uint32_t>(mapEntries.size());
        fragmentSpecializationInfo.pMapEntries   = mapEntries.data();
        fragmentSpecializationInfo.dataSize      = sizeof(FxaaSpecData);
        fragmentSpecializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        init(pLogicalDevice, format, imageExtent, std::move(inputImages), std::move(outputImages), pConfig);
    }

    FxaaEffect::~FxaaEffect() = default;

    const std::vector<EffectParamDesc>& FxaaEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "fxaaQualitySubpix", .label = "Subpixel Smoothing", .type = ParamType::Float,
             .defaultVal = 0.75, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
             .category = "Anti-Aliasing", SPEC(0, subpix)},

            {.key = "fxaaQualityEdgeThreshold", .label = "Edge Threshold", .type = ParamType::Float,
             .defaultVal = 0.125, .minVal = 0.0, .maxVal = 1.0, .step = 0.001,
             .category = "Anti-Aliasing", SPEC(1, edgeThreshold)},

            {.key = "fxaaQualityEdgeThresholdMin", .label = "Edge Threshold Min", .type = ParamType::Float,
             .defaultVal = 0.0312, .minVal = 0.0, .maxVal = 1.0, .step = 0.001,
             .category = "Anti-Aliasing", SPEC(2, edgeThresholdMin)},
        };
        return params;
    }

} // namespace vkBasalt
