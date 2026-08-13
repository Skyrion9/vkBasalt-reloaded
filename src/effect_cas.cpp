#include "effect_cas.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
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

        struct CasSpecData {
            float sharpness;
            int32_t hdrMode;
        };

        CasSpecData specData;
        specData.sharpness = std::clamp(pConfig->getOption<float>("casSharpness", 0.4f), 0.0f, 1.0f);
        specData.hdrMode   = isHDR ? 1 : 0;

        m_paramValues["casSharpness"] = specData.sharpness;

        std::array<VkSpecializationMapEntry, 2> mapEntries = {{
            {0, offsetof(CasSpecData, sharpness), sizeof(float)},
            {1, offsetof(CasSpecData, hdrMode),   sizeof(int32_t)}
        }};

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = mapEntries.size();
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
            {"casSharpness", "Sharpness", ParamType::Float, 0.4, 0.0, 1.0, 0.01},
        };
        return params;
    }
} // namespace vkBasalt
