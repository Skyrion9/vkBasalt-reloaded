#include "effect_fxaa.hpp"

#include <cstring>
#include <algorithm>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
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

        float fxaaQualitySubpix           = std::clamp(pConfig->getOption<float>("fxaaQualitySubpix", 0.75f), 0.0f, 1.0f);
        float fxaaQualityEdgeThreshold    = std::clamp(pConfig->getOption<float>("fxaaQualityEdgeThreshold", 0.125f), 0.0f, 1.0f);
        float fxaaQualityEdgeThresholdMin = std::clamp(pConfig->getOption<float>("fxaaQualityEdgeThresholdMin", 0.0312f), 0.0f, 1.0f);

        m_paramValues["fxaaQualitySubpix"]           = fxaaQualitySubpix;
        m_paramValues["fxaaQualityEdgeThreshold"]    = fxaaQualityEdgeThreshold;
        m_paramValues["fxaaQualityEdgeThresholdMin"] = fxaaQualityEdgeThresholdMin;

        std::vector<VkSpecializationMapEntry> specMapEntrys(5);

        for (uint32_t i = 0; i < specMapEntrys.size(); i++)
        {
            specMapEntrys[i].constantID = i;
            specMapEntrys[i].offset     = sizeof(float) * i;
            specMapEntrys[i].size       = sizeof(float);
        }
        std::vector<float> specData = {
            fxaaQualitySubpix, fxaaQualityEdgeThreshold, fxaaQualityEdgeThresholdMin, (float) imageExtent.width, (float) imageExtent.height};

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = specMapEntrys.size();
        fragmentSpecializationInfo.pMapEntries   = specMapEntrys.data();
        fragmentSpecializationInfo.dataSize      = sizeof(float) * specData.size();
        fragmentSpecializationInfo.pData         = specData.data();

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }
    
    FxaaEffect::~FxaaEffect()
    {
    }

    const std::vector<EffectParamDesc>& FxaaEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {"fxaaQualitySubpix",           "Subpixel Smoothing",  ParamType::Float, 0.75,   0.0,  1.0,  0.01, {}, "Anti-Aliasing"},
            {"fxaaQualityEdgeThreshold",    "Edge Threshold",      ParamType::Float, 0.125,  0.0,  1.0, 0.001, {}, "Anti-Aliasing"},
            {"fxaaQualityEdgeThresholdMin", "Edge Threshold Min",  ParamType::Float, 0.0312, 0.0,  1.0, 0.001, {}, "Anti-Aliasing"},
        };
        return params;
    }

    double FxaaEffect::getParam(const std::string& key) const {
        auto it = m_paramValues.find(key);
        return (it != m_paramValues.end()) ? it->second : 0.0;
    }

    bool FxaaEffect::setParam(const std::string& key, double value) {
        auto it = m_paramValues.find(key);
        if (it == m_paramValues.end()) return false;
        if (it->second == value) return false;
        it->second = value;
        return true;
    }

} // namespace vkBasalt
