#include "effect_fxaa.hpp"

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
#include "shader_sources.hpp"

namespace vkBasalt
{

    struct FxaaSpecData {
        float subpix;
        float edgeThreshold;
        float edgeThresholdMin;
        float screenWidth;
        float screenHeight;
    };

    #define SPEC(id, field) id, offsetof(FxaaSpecData, field), sizeof(((FxaaSpecData*)0)->field)

    FxaaEffect::FxaaEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = fxaa_frag;

        // Prevent the pipeline layout from allocating a push constant range, tells SimpleEffect::applyEffect to skip the CmdPushConstants API call.
        this->pushConstantSize = 0;

        const auto& params = getParamDescs();
        FxaaSpecData specData = {};
        std::vector<VkSpecializationMapEntry> mapEntries;
        mapEntries.reserve(params.size() + 2);

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

        specData.screenWidth  = (float)imageExtent.width;
        specData.screenHeight = (float)imageExtent.height;

        mapEntries.push_back({3, offsetof(FxaaSpecData, screenWidth),  sizeof(float)});
        mapEntries.push_back({4, offsetof(FxaaSpecData, screenHeight), sizeof(float)});


        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = (uint32_t)mapEntries.size();
        fragmentSpecializationInfo.pMapEntries   = mapEntries.data();
        fragmentSpecializationInfo.dataSize      = sizeof(FxaaSpecData);
        fragmentSpecializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    FxaaEffect::~FxaaEffect() {}

    const std::vector<EffectParamDesc>& FxaaEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"fxaaQualitySubpix",           "Subpixel Smoothing", ParamType::Float, 0.75,   0.0, 1.0, 0.01,  {}, "Anti-Aliasing", SPEC(0, subpix)},
            {"fxaaQualityEdgeThreshold",    "Edge Threshold",     ParamType::Float, 0.125,  0.0, 1.0, 0.001, {}, "Anti-Aliasing", SPEC(1, edgeThreshold)},
            {"fxaaQualityEdgeThresholdMin", "Edge Threshold Min", ParamType::Float, 0.0312, 0.0, 1.0, 0.001, {}, "Anti-Aliasing", SPEC(2, edgeThresholdMin)},
        };
        return params;
    }

} // namespace vkBasalt
