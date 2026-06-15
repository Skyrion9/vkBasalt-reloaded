#include "effect_clarityrcas.hpp"

#include <cstring>
#include <cstddef> // Required for offsetof

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
    ClarityRcasEffect::ClarityRcasEffect(LogicalDevice*       pLogicalDevice,
                                         VkFormat             format,
                                         VkExtent2D           imageExtent,
                                         std::vector<VkImage> inputImages,
                                         std::vector<VkImage> outputImages,
                                         Config*              pConfig)
    {
        Logger::debug("in creating ClarityRcasEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarityrcas_frag;

        struct ClarityRcasSpecData {
            float radius;
            float offset;
            float clarityStrength;
            int32_t blendMode;
            int32_t blendIfDark;
            int32_t blendIfLight;
            float casSharpness;
            float casStrength;
        };

        // Create instance and populate with config values
        ClarityRcasSpecData specData;
        // Shared Clarity settings (reused from clarityCas)
        specData.radius          = pConfig->getOption<float>("clarityCasRadius", 1.0f);
        specData.offset          = pConfig->getOption<float>("clarityCasOffset", 5.0f);
        specData.clarityStrength = pConfig->getOption<float>("clarityCasStrength", 1.0f);
        specData.blendMode       = pConfig->getOption<int32_t>("clarityCasBlendMode", 6);
        specData.blendIfDark     = pConfig->getOption<int32_t>("clarityCasBlendIfDark", 50);
        specData.blendIfLight    = pConfig->getOption<int32_t>("clarityCasBlendIfLight", 215);
        // Exclusive RCAS settings
        specData.casSharpness    = pConfig->getOption<float>("clarityRcasCasSharpness", 0.4f);
        specData.casStrength     = pConfig->getOption<float>("clarityRcasCasStrength", 1.0f);

        // Map struct fields to GLSL constant IDs using offsetof
        VkSpecializationMapEntry mapEntries[8] = {
            {0, offsetof(ClarityRcasSpecData, radius),          sizeof(float)},
            {1, offsetof(ClarityRcasSpecData, offset),          sizeof(float)},
            {2, offsetof(ClarityRcasSpecData, clarityStrength), sizeof(float)},
            {3, offsetof(ClarityRcasSpecData, blendMode),        sizeof(int32_t)},
            {4, offsetof(ClarityRcasSpecData, blendIfDark),     sizeof(int32_t)},
            {5, offsetof(ClarityRcasSpecData, blendIfLight),    sizeof(int32_t)},
            {6, offsetof(ClarityRcasSpecData, casSharpness),    sizeof(float)},
            {7, offsetof(ClarityRcasSpecData, casStrength),     sizeof(float)}
        };

        // Setup specialization info with correct data size
        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 8;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(ClarityRcasSpecData);
        specializationInfo.pData         = &specData;

        // Assign to SimpleEffect's protected pointers before calling base init
        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        // Call base class init to setup pipelines, framebuffers, etc.
        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    ClarityRcasEffect::~ClarityRcasEffect()
    {
        // Base class SimpleEffect::~SimpleEffect() handles all the Vulkan resource cleanup.
    }

} // namespace vkBasalt
