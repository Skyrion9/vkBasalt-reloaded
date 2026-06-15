#include "effect_claritycas.hpp"

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
    ClarityCasEffect::ClarityCasEffect(LogicalDevice*       pLogicalDevice,
                                       VkFormat             format,
                                       VkExtent2D           imageExtent,
                                       std::vector<VkImage> inputImages,
                                       std::vector<VkImage> outputImages,
                                       Config*              pConfig)
    {
        Logger::debug("in creating ClarityCasEffect");

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = claritycas_frag;

        struct ClarityCasSpecData {
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
        ClarityCasSpecData specData;
        specData.radius          = pConfig->getOption<float>("clarityCasRadius", 1.0f);
        specData.offset          = pConfig->getOption<float>("clarityCasOffset", 5.0f);
        specData.clarityStrength = pConfig->getOption<float>("clarityCasStrength", 1.0f);
        specData.blendMode       = pConfig->getOption<int32_t>("clarityCasBlendMode", 6);
        specData.blendIfDark     = pConfig->getOption<int32_t>("clarityCasBlendIfDark", 50);
        specData.blendIfLight    = pConfig->getOption<int32_t>("clarityCasBlendIfLight", 215);
        specData.casSharpness    = pConfig->getOption<float>("clarityCasCasSharpness", 0.4f);
        specData.casStrength     = pConfig->getOption<float>("clarityCasCasStrength", 1.0f);

        // Map struct fields to GLSL constant IDs using offsetof
        VkSpecializationMapEntry mapEntries[8] = {
            {0, offsetof(ClarityCasSpecData, radius),          sizeof(float)},
            {1, offsetof(ClarityCasSpecData, offset),          sizeof(float)},
            {2, offsetof(ClarityCasSpecData, clarityStrength), sizeof(float)},
            {3, offsetof(ClarityCasSpecData, blendMode),       sizeof(int32_t)},
            {4, offsetof(ClarityCasSpecData, blendIfDark),     sizeof(int32_t)},
            {5, offsetof(ClarityCasSpecData, blendIfLight),    sizeof(int32_t)},
            {6, offsetof(ClarityCasSpecData, casSharpness),    sizeof(float)},
            {7, offsetof(ClarityCasSpecData, casStrength),     sizeof(float)}
        };

        // Setup specialization info with correct data size
        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 8;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(ClarityCasSpecData);
        specializationInfo.pData         = &specData;

        // Assign to SimpleEffect's protected pointers before calling base init
        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        // Call base class init to setup pipelines, framebuffers, etc.
        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    ClarityCasEffect::~ClarityCasEffect()
    {
        // Base class SimpleEffect::~SimpleEffect() handles all the Vulkan resource cleanup.
    }

} // namespace vkBasalt