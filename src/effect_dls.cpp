#include "effect_dls.hpp"
#include <cstring>
#include <algorithm>
#include <string>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "format.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
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

        bool isHDR = (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                      colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT ||
                      colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT ||
                      colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
                      isExtendedRangeFormat(format));

        struct DlsSpecData {
            float sharpen;
            float denoise;
            int32_t hdrMode;
        };

        DlsSpecData specData;
        specData.sharpen  = std::clamp(pConfig->getOption<float>("dlsSharpness", 0.5f), 0.0f, 1.0f);
        specData.denoise  = std::clamp(pConfig->getOption<float>("dlsDenoise", 0.17f), 0.0f, 1.0f);
        specData.hdrMode  = isHDR ? 1 : 0;

        m_paramValues["dlsSharpness"] = specData.sharpen;
        m_paramValues["dlsDenoise"]   = specData.denoise;

        std::array<VkSpecializationMapEntry, 3> mapEntries = {{
            {0, offsetof(DlsSpecData, sharpen),  sizeof(float)},
            {1, offsetof(DlsSpecData, denoise),  sizeof(float)},
            {2, offsetof(DlsSpecData, hdrMode),  sizeof(int32_t)}
        }};

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = mapEntries.size();
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
            {"dlsSharpness", "Sharpness", ParamType::Float, 0.5,  0.0, 1.0, 0.01},
            {"dlsDenoise",   "Denoise",   ParamType::Float, 0.17, 0.0, 1.0, 0.01},
        };
        return params;
    }

} // namespace vkBasalt
