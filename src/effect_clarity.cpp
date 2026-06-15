#include "effect_clarity.hpp"

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
    ClarityEffect::ClarityEffect(LogicalDevice*       pLogicalDevice,
                                 VkFormat             format,
                                 VkExtent2D           imageExtent,
                                 std::vector<VkImage> inputImages,
                                 std::vector<VkImage> outputImages,
                                 Config*              pConfig)
    {
        Logger::debug("in creating ClarityEffect");

        struct ClaritySpecData {
            float radius;
            float offset;
            float strength;
            int32_t blendMode;
            int32_t blendIfDark;
            int32_t blendIfLight;
        };

        // Populate with config values
        ClaritySpecData specData;
        specData.radius         = pConfig->getOption<float>("clarityRadius", 1.0f);
        specData.offset         = pConfig->getOption<float>("clarityOffset", 1.0f);
        specData.strength       = pConfig->getOption<float>("clarityStrength", 0.160f);
        specData.blendMode      = pConfig->getOption<int32_t>("clarityBlendMode", 6);
        specData.blendIfDark    = pConfig->getOption<int32_t>("clarityBlendIfDark", 50);
        specData.blendIfLight   = pConfig->getOption<int32_t>("clarityBlendIfLight", 215);

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = clarity_frag;

        // 3Map struct fields to GLSL constant IDs using offsetof
        VkSpecializationMapEntry mapEntries[6] = {
            {0, offsetof(ClaritySpecData, radius),         sizeof(float)},
            {1, offsetof(ClaritySpecData, offset),         sizeof(float)},
            {2, offsetof(ClaritySpecData, strength),       sizeof(float)},
            {3, offsetof(ClaritySpecData, blendMode),      sizeof(int32_t)},
            {4, offsetof(ClaritySpecData, blendIfDark),    sizeof(int32_t)},
            {5, offsetof(ClaritySpecData, blendIfLight),   sizeof(int32_t)}
        };

        // 4. Setup specialization info with correct data size
        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 6;
        specializationInfo.pMapEntries   = mapEntries;
        specializationInfo.dataSize      = sizeof(ClaritySpecData);
        specializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &specializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }

    ClarityEffect::~ClarityEffect()
    {
    }

} // namespace vkBasalt