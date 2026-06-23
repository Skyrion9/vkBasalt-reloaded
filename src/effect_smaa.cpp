#include "effect_smaa.hpp"

#include <cstring>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "image.hpp"
#include "util.hpp"

#include "AreaTex.h"
#include "SearchTex.h"
#include "shader_sources.hpp"

namespace vkBasalt
{
    SmaaEffect::SmaaEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig)
    {
        Logger::debug("in creating SmaaEffect");

        this->pLogicalDevice = pLogicalDevice;
        this->format         = format;
        this->imageExtent    = imageExtent;
        this->inputImages    = inputImages;
        this->outputImages   = outputImages;
        this->pConfig        = pConfig;

        // create Images for the first and second pass at once -> less memory fragmentation
        std::vector<VkImage> edgeAndBlendImages = createImages(pLogicalDevice,
                                                               inputImages.size() * 2,
                                                               {imageExtent.width, imageExtent.height, 1},
                                                               VK_FORMAT_B8G8R8A8_UNORM, // TODO search for format and save it
                                                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               imageMemory);

        edgeImages  = std::vector<VkImage>(edgeAndBlendImages.begin(), edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2);
        blendImages = std::vector<VkImage>(edgeAndBlendImages.begin() + edgeAndBlendImages.size() / 2, edgeAndBlendImages.end());

        inputImageViews = createImageViews(pLogicalDevice, format, inputImages);
        Logger::debug("created input ImageViews");
        edgeImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, edgeImages);
        Logger::debug("created edge  ImageViews");
        blendImageViews = createImageViews(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, blendImages);
        Logger::debug("created blend ImageViews");
        outputImageViews = createImageViews(pLogicalDevice, format, outputImages);
        Logger::debug("created output ImageViews");
        sampler = createSampler(pLogicalDevice);
        Logger::debug("created sampler");

        VkExtent3D areaImageExtent = {AREATEX_WIDTH, AREATEX_HEIGHT, 1};
        areaImage = createImages(pLogicalDevice, 1, areaImageExtent, VK_FORMAT_R8G8_UNORM, 
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, areaMemory)[0];

        VkExtent3D searchImageExtent = {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1};
        searchImage = createImages(pLogicalDevice, 1, searchImageExtent, VK_FORMAT_R8_UNORM, 
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, searchMemory)[0];

        uploadToImage(pLogicalDevice, areaImage, areaImageExtent, AREATEX_SIZE, areaTexBytes);
        uploadToImage(pLogicalDevice, searchImage, searchImageExtent, SEARCHTEX_SIZE, searchTexBytes);

        areaImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8G8_UNORM, std::vector<VkImage>(1, areaImage))[0];
        Logger::debug("after creating area ImageView");
        searchImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8_UNORM, std::vector<VkImage>(1, searchImage))[0];
        Logger::debug("created search ImageView");

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 5);
        Logger::debug("created descriptorSetLayouts");

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = inputImages.size() * 5;
        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        descriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);
        Logger::debug("created descriptorPool");

        // get config options
        struct SmaaOptions
        {
            float   screenWidth;
            float   screenHeight;
            float   reverseScreenWidth;
            float   reverseScreenHeight;
            float   threshold;
            int32_t maxSearchSteps;
            int32_t maxSearchStepsDiag;
            int32_t cornerRounding;
            int32_t disableDiagDetection;
        };

        SmaaOptions smaaOptions;

        // Apply SMAA presets first if specified, ignore individual settings when preset is used
        std::string preset = pConfig->getOption<std::string>("smaaPreset", "");

        if (preset == "low") {
            smaaOptions.threshold = 0.15f; smaaOptions.maxSearchSteps = 4; smaaOptions.maxSearchStepsDiag = 0;
            smaaOptions.cornerRounding = 25; smaaOptions.disableDiagDetection = 1;
        } else if (preset == "medium") {
            smaaOptions.threshold = 0.10f; smaaOptions.maxSearchSteps = 8; smaaOptions.maxSearchStepsDiag = 0;
            smaaOptions.cornerRounding = 25; smaaOptions.disableDiagDetection = 1;
        } else if (preset == "high") {
            smaaOptions.threshold = 0.10f; smaaOptions.maxSearchSteps = 16; smaaOptions.maxSearchStepsDiag = 8;
            smaaOptions.cornerRounding = 25; smaaOptions.disableDiagDetection = 0;
        } else if (preset == "ultra") {
            smaaOptions.threshold = 0.05f; smaaOptions.maxSearchSteps = 32; smaaOptions.maxSearchStepsDiag = 16;
            smaaOptions.cornerRounding = 25; smaaOptions.disableDiagDetection = 0;
        } else {
            smaaOptions.threshold = pConfig->getOption<float>("smaaThreshold", 0.05f);
            smaaOptions.maxSearchSteps = pConfig->getOption<int32_t>("smaaMaxSearchSteps", 32);
            smaaOptions.maxSearchStepsDiag = pConfig->getOption<int32_t>("smaaMaxSearchStepsDiag", 16);
            smaaOptions.cornerRounding = pConfig->getOption<int32_t>("smaaCornerRounding", 25);
            smaaOptions.disableDiagDetection = pConfig->getOption<int32_t>("smaaDisableDiagDetection", 0);
        }

        createShaderModule(pLogicalDevice, smaa_edge_vert, &edgeVertexModule);
        bool useColor = pConfig->getOption<std::string>("smaaEdgeDetection", "luma") == "color";
        auto shaderCode = useColor ? smaa_edge_color_frag : smaa_edge_luma_frag;
        createShaderModule(pLogicalDevice, shaderCode, &edgeFragmentModule);

        createShaderModule(pLogicalDevice, smaa_blend_vert, &blendVertexModule);
        createShaderModule(pLogicalDevice, smaa_blend_frag, &blendFragmentModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_vert, &neighborVertexModule);
        createShaderModule(pLogicalDevice, smaa_neighbor_frag, &neighborFragmentModule);

        renderPass      = createRenderPass(pLogicalDevice, format, false, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        unormRenderPass = createRenderPass(pLogicalDevice, VK_FORMAT_B8G8R8A8_UNORM, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {imageSamplerDescriptorSetLayout};
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        std::vector<VkSpecializationMapEntry> specMapEntrys(9);
        for (uint32_t i = 0; i < specMapEntrys.size(); i++)
        {
            specMapEntrys[i].constantID = i;
            specMapEntrys[i].offset     = sizeof(float) * i; // TODO not clean to assume that sizeof(int32_t) == sizeof(float)
            specMapEntrys[i].size       = sizeof(float);
        }
        
        smaaOptions.screenWidth = (float) imageExtent.width;
        smaaOptions.screenHeight = (float) imageExtent.height;
        smaaOptions.reverseScreenWidth  = 1.0f / imageExtent.width;
        smaaOptions.reverseScreenHeight = 1.0f / imageExtent.height;

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = specMapEntrys.size();
        specializationInfo.pMapEntries   = specMapEntrys.data();
        specializationInfo.dataSize      = sizeof(smaaOptions);
        specializationInfo.pData         = &smaaOptions;

        edgePipeline = createGraphicsPipeline(pLogicalDevice, edgeVertexModule, &specializationInfo, "main",
                                              edgeFragmentModule, &specializationInfo, "main",
                                              imageExtent, unormRenderPass, pipelineLayout);

        blendPipeline = createGraphicsPipeline(pLogicalDevice, blendVertexModule, &specializationInfo, "main",
                                               blendFragmentModule, &specializationInfo, "main",
                                               imageExtent, unormRenderPass, pipelineLayout);

        neighborPipeline = createGraphicsPipeline(pLogicalDevice, neighborVertexModule, &specializationInfo, "main",
                                                  neighborFragmentModule, &specializationInfo, "main",
                                                  imageExtent, renderPass, pipelineLayout);

        std::vector<std::vector<VkImageView>> imageViewsVector = {
            inputImageViews, edgeImageViews,
            std::vector<VkImageView>(inputImageViews.size(), areaImageView),
            std::vector<VkImageView>(inputImageViews.size(), searchImageView),
            blendImageViews
        };

        imageDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice, descriptorPool,
                                                                         imageSamplerDescriptorSetLayout,
                                                                         std::vector<VkSampler>(imageViewsVector.size(), sampler),
                                                                         imageViewsVector);

        edgeFramebuffers     = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {edgeImageViews});
        blendFramebuffers    = createFramebuffers(pLogicalDevice, unormRenderPass, imageExtent, {blendImageViews});
        neighborFramebuffers = createFramebuffers(pLogicalDevice, renderPass, imageExtent, {outputImageViews});
    }

    void SmaaEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        Logger::debug("applying smaa effect to cb " + convertToString(commandBuffer));
        
        // Barrier 1 inputImage -> SHADER_READ_ONLY
        VkImageMemoryBarrier barrier1 = {};
        barrier1.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // Wait for the game's color/transfer writes to finish and become visible
        barrier1.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier1.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier1.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier1.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.image               = inputImages[imageIndex];
        barrier1.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier1);
        Logger::debug("after the first pipeline barrier SMAA");

        VkRenderPassBeginInfo renderPassBeginInfo = {}; 
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        // Alpha 1f
        VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassBeginInfo.clearValueCount   = 1;
        renderPassBeginInfo.pClearValues      = &clearValue;

        // Pass 1 edge detection
        renderPassBeginInfo.renderPass  = unormRenderPass;
        renderPassBeginInfo.framebuffer = edgeFramebuffers[imageIndex];

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, edgePipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Barrier 2 edge image memory visibility
        VkImageMemoryBarrier barrier2 = {};
        barrier2.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier2.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier2.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier2.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
        barrier2.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image               = edgeImages[imageIndex];
        barrier2.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier2);

        // pass 2 blend weight calculation
        renderPassBeginInfo.framebuffer = blendFramebuffers[imageIndex];
        
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // barrier 3 blend image memory visibility
        VkImageMemoryBarrier barrier3 = {};
        barrier3.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier3.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier3.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier3.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
        barrier3.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier3.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier3.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier3.image               = blendImages[imageIndex];
        barrier3.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier3);

        // pass 3 neighborhood blending
        renderPassBeginInfo.framebuffer = neighborFramebuffers[imageIndex];
        renderPassBeginInfo.renderPass  = renderPass; 
        renderPassBeginInfo.clearValueCount = 0; 
        renderPassBeginInfo.pClearValues    = nullptr;
        
        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, neighborPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // barrier 4 inputImage -> PRESENT_SRC_KHR
        VkImageMemoryBarrier barrier4 = {};
        barrier4.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier4.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier4.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
        barrier4.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier4.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier4.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier4.image               = inputImages[imageIndex];
        barrier4.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier4);
            Logger::debug("after SMAA barrier 4");
    }

    SmaaEffect::~SmaaEffect()
    {
        Logger::debug("destroying smaa effect " + convertToString(this));
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, edgePipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, blendPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, neighborPipeline, nullptr);

        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, unormRenderPass, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);

        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, edgeVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, edgeFragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, blendVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, blendFragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, neighborVertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, neighborFragmentModule, nullptr);

        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, imageMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, areaMemory, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, searchMemory, nullptr);
        for (unsigned int i = 0; i < edgeFramebuffers.size(); i++)
        {
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, edgeFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, blendFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, neighborFramebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, edgeImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, blendImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, edgeImages[i], nullptr);
            pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, blendImages[i], nullptr);
        }
        Logger::debug("after SMAA DestroyImageView");
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, areaImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, areaImage, nullptr);
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, searchImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, searchImage, nullptr);

        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
    }
} // namespace vkBasalt
