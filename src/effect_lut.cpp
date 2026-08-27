#include "effect_lut.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "stb_image.h"
#include <vulkan/vulkan_core.h>

#include "config.hpp"
#include "effect.hpp"
#include "effect_simple.hpp"
#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "logical_device.hpp"
#include "image.hpp"
#include "lut_cube.hpp"

#include "format.hpp"
#include "shader_sources.hpp"
#include "logger.hpp"

namespace vkBasalt
{

    struct LutSpecData {
        int32_t lutSize;
        int32_t flipGB;
        int32_t colorSpaceMode;
    };

    LutEffect::LutEffect(LogicalDevice*       pLogicalDevice,
                         VkFormat             format,
                         VkExtent2D           imageExtent,
                         std::vector<VkImage> inputImages,
                         std::vector<VkImage> outputImages,
                         Config*              pConfig,
                         VkColorSpaceKHR      colorSpace)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = lut_frag;
        this->pushConstantSize = 0;

        ColorSpaceMode csm = getColorSpaceMode(format, colorSpace);

        std::string lutFile = pConfig->getOption<std::string>("lutFile", "");
        m_paramValues["lutFile"] = 0.0; // FilePath type, value unused in m_paramValues

        int      height = 0;
        LutCube  lutCube;
        stbi_uc* pixels = nullptr;
        int32_t  usingPNG = 0;
        bool     freePixels = false;
        bool fileValid = false;

        if (!lutFile.empty()) {
            std::ifstream testFile(lutFile);
            fileValid = testFile.good();
        }

        // 2x2x2 LUT pass through fallback when no file is configured or when loading/parsing fails.
        static stbi_uc identityLut[2*2*2*4] = {
            0,0,0,255,       255,0,0,255,
            0,255,0,255,     255,255,0,255,
            0,0,255,255,     255,0,255,255,
            0,255,255,255,   255,255,255,255
        };

        auto useIdentityLut = [&]() {
            pixels     = identityLut;
            height     = 2;
            usingPNG   = 0;
            freePixels = false;
        };

        if (!fileValid) {
            if (lutFile.empty()) {
                Logger::warn("LUT effect enabled but no lutFile configured. Using identity LUT (pass-through).");
            } else {
                Logger::warn("LUT file not found: " + lutFile + ". Using identity LUT (pass-through).");
            }
            useIdentityLut();
        } else {
            usingPNG = (int32_t)(lutFile.find(".cube") == std::string::npos && lutFile.find(".CUBE") == std::string::npos);
            if (!usingPNG) {
                lutCube = LutCube(lutFile);
                if (lutCube.size == 0) {
                    Logger::err("Failed to parse LUT cube: " + lutFile + ". Falling back to identity LUT.");
                    useIdentityLut();
                } else {
                    pixels = lutCube.colorCube.data();
                    height = lutCube.size;
                }
            } else {
                int channels, width;
                pixels = stbi_load(lutFile.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                if (!pixels) {
                    Logger::err("Failed to load LUT PNG: " + lutFile + ". Falling back to identity LUT.");
                    useIdentityLut();
                } else if (width != height * height) {
                    Logger::err("Invalid LUT PNG dimensions (width must equal height * height). Falling back to identity LUT.");
                    stbi_image_free(pixels);
                    useIdentityLut();
                } else {
                    freePixels = true;
                }
            }
        }

        LutSpecData specData = {};
        specData.lutSize = height;
        specData.flipGB  = 0;
        specData.colorSpaceMode = static_cast<int32_t>(csm);

        VkSpecializationMapEntry mapEntries[] = {
            {0, offsetof(LutSpecData, lutSize), sizeof(int32_t)},
            {1, offsetof(LutSpecData, flipGB),  sizeof(int32_t)},
            {65535, offsetof(LutSpecData, colorSpaceMode), sizeof(int32_t)}
        };

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = sizeof(mapEntries) / sizeof(mapEntries[0]);
        fragmentSpecializationInfo.pMapEntries   = mapEntries;
        fragmentSpecializationInfo.dataSize      = sizeof(LutSpecData);
        fragmentSpecializationInfo.pData         = &specData;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        VkExtent3D lutImageExtent = {(uint32_t)height, (uint32_t)height, (uint32_t)height};
        lutImage = createImages(pLogicalDevice, 1, lutImageExtent,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lutMemory)[0];

        uploadToImage(pLogicalDevice, lutImage, lutImageExtent,
                    static_cast<uint32_t>(height) * height * height * 4, pixels);

        if (freePixels) {
            stbi_image_free(pixels);
        }

        lutImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8G8B8A8_UNORM,
                                        std::vector<VkImage>(1, lutImage), VK_IMAGE_VIEW_TYPE_3D)[0];

        lutDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 1);
        descriptorSetLayouts.push_back(lutDescriptorSetLayout);

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = 1;
        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        lutDescriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);

        lutDescriptorSet = allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice,
                            lutDescriptorPool, lutDescriptorSetLayout, {sampler},
                            std::vector<std::vector<VkImageView>>(1, std::vector<VkImageView>(1, lutImageView)))[0];
    }

    LutEffect::~LutEffect()
    {
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, lutImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, lutImage, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, lutDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, lutDescriptorPool, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, lutMemory, nullptr);
    }

    const std::vector<EffectParamDesc>& LutEffect::getParamDescs() const {
        static const std::vector<EffectParamDesc> params = {
            {.key = "lutFile", .label = "LUT File (.cube/.png)", .type = ParamType::FilePath,
             .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.0, .step = 0.0,
             .category = "Color Grading",
             .tooltip = "Path to a color lookup table file. Supports .cube (ReShade-compatible 3D LUT) and .png (tiled 2D strip). "
                        "Apply cinematic color grades, film emulation, or per-game color correction. "
                        "If empty or invalid, the effect acts as a pass-through (identity LUT)."},
        };
        return params;
    }

    void LutEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &(lutDescriptorSet), 0, nullptr);
        SimpleEffect::applyEffect(imageIndex, commandBuffer);
    }
} // namespace vkBasalt
