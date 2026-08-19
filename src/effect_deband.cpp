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

        DebandSpecData specData = {};

        specData.screenWidth         = (float)imageExtent.width;
        specData.screenHeight        = (float)imageExtent.height;
        specData.reverseScreenWidth  = 1.0f / imageExtent.width;
        specData.reverseScreenHeight = 1.0f / imageExtent.height;
        specData.debandAvgdiff       = std::clamp(pConfig->getOption<float>("debandAvgdiff", 3.4f), 0.0f, 20.0f);
        specData.debandMaxdiff       = std::clamp(pConfig->getOption<float>("debandMaxdiff", 6.8f), 0.0f, 40.0f);
        specData.debandMiddiff       = std::clamp(pConfig->getOption<float>("debandMiddiff", 3.3f), 0.0f, 20.0f);
        specData.range               = std::clamp(pConfig->getOption<float>("debandRange", 16.0f), 1.0f, 64.0f);
        specData.iterations          = std::clamp(pConfig->getOption<int32_t>("debandIterations", 4), 1, 8);
        specData.hdrMode             = isHDR ? 1 : 0;

        m_paramValues["debandAvgdiff"]    = specData.debandAvgdiff;
        m_paramValues["debandMaxdiff"]    = specData.debandMaxdiff;
        m_paramValues["debandMiddiff"]    = specData.debandMiddiff;
        m_paramValues["debandRange"]      = specData.range;
        m_paramValues["debandIterations"] = specData.iterations;

        // mapEntries must be a stack allocated C-array. std::vector causes GPU hangs on AMD RADV when specialization constants are used as shader iteration/loop bounds.
        VkSpecializationMapEntry mapEntries[] = {
            {0, offsetof(DebandSpecData, screenWidth),         sizeof(float)},
            {1, offsetof(DebandSpecData, screenHeight),        sizeof(float)},
            {2, offsetof(DebandSpecData, reverseScreenWidth),  sizeof(float)},
            {3, offsetof(DebandSpecData, reverseScreenHeight), sizeof(float)},
            {4, offsetof(DebandSpecData, debandAvgdiff),       sizeof(float)},
            {5, offsetof(DebandSpecData, debandMaxdiff),       sizeof(float)},
            {6, offsetof(DebandSpecData, debandMiddiff),       sizeof(float)},
            {7, offsetof(DebandSpecData, range),               sizeof(float)},
            {8, offsetof(DebandSpecData, iterations),          sizeof(int32_t)},
            {9, offsetof(DebandSpecData, hdrMode),             sizeof(int32_t)}
        };

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = sizeof(mapEntries) / sizeof(mapEntries[0]);
        specializationInfo.pMapEntries   = mapEntries;
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
