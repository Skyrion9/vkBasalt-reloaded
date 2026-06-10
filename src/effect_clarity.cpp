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
        float clarityStrength = pConfig->getOption<float>("clarityStrength", 0.4f);
        float clarityRadius   = pConfig->getOption<float>("clarityRadius", 2.5f);

        float specData[2] = {clarityStrength, clarityRadius};

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarity_frag;

        VkSpecializationMapEntry mapEntries[2];
        mapEntries[0].constantID = 0;
        mapEntries[0].offset     = 0;
        mapEntries[0].size       = sizeof(float);
        mapEntries[1].constantID = 1;
        mapEntries[1].offset     = sizeof(float);
        mapEntries[1].size       = sizeof(float);

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = 2;
        fragmentSpecializationInfo.pMapEntries   = mapEntries;
        fragmentSpecializationInfo.dataSize      = sizeof(float) * 2;
        fragmentSpecializationInfo.pData         = specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }
    ClarityEffect::~ClarityEffect()
    {
    }
} // namespace vkBasalt