#include "effect_clarity.hpp"

#include <cstring>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "util.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
    ClarityEffect::ClarityEffect(LogicalDevice*       pLogicalDevice,
                                  VkFormat             format,
                                  VkExtent2D           imageExtent,
                                  std::vector<VkImage> inputImages,
                                  std::vector<VkImage> outputImages,
                                  Config*              pConfig)
    {
        Logger::debug("in creating ClarityEffect");

        // Get all config options
        float clarityStrength    = pConfig->getOption<float>("clarityStrength", 0.160f);
        float clarityRadius      = pConfig->getOption<float>("clarityRadius", 1.0f);
        float clarityOffset      = pConfig->getOption<float>("clarityOffset", 1.0f);
        int32_t clarityBlendMode = pConfig->getOption<int32_t>("clarityBlendMode", 6);
        int32_t clarityBlendIfDark = pConfig->getOption<int32_t>("clarityBlendIfDark", 50);
        int32_t clarityBlendIfLight = pConfig->getOption<int32_t>("clarityBlendIfLight", 215);
        float clarityDarkIntensity = pConfig->getOption<float>("clarityDarkIntensity", 0.160f);
        float clarityLightIntensity = pConfig->getOption<float>("clarityLightIntensity", 0.0f);

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarity_frag;

        // Prepare specialization constants for all 8 parameters
        float specData[8] = {
            clarityRadius,
            clarityOffset,
            clarityStrength,
            float(clarityBlendMode),
            float(clarityBlendIfDark),
            float(clarityBlendIfLight),
            clarityDarkIntensity,
            clarityLightIntensity
        };

        VkSpecializationMapEntry mapEntries[8];
        for (int i = 0; i < 8; i++) {
            mapEntries[i].constantID = i;
            mapEntries[i].offset     = sizeof(float) * i;
            mapEntries[i].size       = sizeof(float);
        }

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 8;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(float) * 8;
        specializationInfo.pData         = specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }
    ClarityEffect::~ClarityEffect()
    {
    }
} // namespace vkBasalt