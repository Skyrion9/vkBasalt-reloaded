#include "effect_simple.hpp"

#include <cstring>
#include <vector>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "util.hpp"

namespace vkBasalt
{
    SimpleEffect::SimpleEffect()
    {
    }
    void SimpleEffect::init(LogicalDevice*       pLogicalDevice,
                            VkFormat             format,
                            VkExtent2D           imageExtent,
                            std::vector<VkImage> inputImages,
                            std::vector<VkImage> outputImages,
                            Config*              pConfig)
    {
        Logger::debug("in creating SimpleEffect");

        this->pLogicalDevice = pLogicalDevice;
        this->format         = format;
        this->imageExtent    = imageExtent;
        this->inputImages    = inputImages;
        this->outputImages   = outputImages;
        this->pConfig        = pConfig;

        inputImageViews = createImageViews(pLogicalDevice, format, inputImages);
        Logger::debug("created input ImageViews");
        outputImageViews = createImageViews(pLogicalDevice, format, outputImages);
        Logger::debug("created ImageViews");
        sampler = createSampler(pLogicalDevice);
        Logger::debug("created sampler");

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 1);
        Logger::debug("created descriptorSetLayouts");

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = inputImages.size() + 10;

        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};

        descriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);
        Logger::debug("created descriptorPool");

        createShaderModule(pLogicalDevice, vertexCode, &vertexModule);
        createShaderModule(pLogicalDevice, fragmentCode, &fragmentModule);

        // Pass the dynamic clear and layout flags to the render pass creator
        renderPass = createRenderPass(pLogicalDevice, format, needsClear, finalLayout);

        descriptorSetLayouts.insert(descriptorSetLayouts.begin(), imageSamplerDescriptorSetLayout);
        
        // Pass the dynamic pushConstantSize to the layout creator
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts, this->pushConstantSize);

        graphicsPipeline = createGraphicsPipeline(pLogicalDevice,
                                                  vertexModule,
                                                  pVertexSpecInfo,
                                                  "main",
                                                  fragmentModule,
                                                  pFragmentSpecInfo,
                                                  "main",
                                                  imageExtent,
                                                  renderPass,
                                                  pipelineLayout);

        imageDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(
            pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout, {sampler}, std::vector<std::vector<VkImageView>>(1, inputImageViews));

        framebuffers = createFramebuffers(pLogicalDevice, renderPass, imageExtent, {outputImageViews});
    }
    void SimpleEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        Logger::debug("applying SimpleEffect to cb " + convertToString(commandBuffer));
        // Used to make the Image accessable by the shader
        VkImageMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT; 
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkImageMemoryBarrier secondBarrier = {};
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = finalLayout; // Respect the final layout defined by the effect
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];
        secondBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        Logger::debug("after the first pipeline barrier");

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;

        // conditionally attach clearValue only if the effect requested it
        VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
        if (needsClear) {
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = &clearValue;
        }

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        Logger::debug("after beginn renderpass");

        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        Logger::debug("after binding image sampler");

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        Logger::debug("after bind pipeliene");

        // Push constant injection
        // Dynamically sized based on the derived effect's pushConstantSize.
        // Shaders that declare a push_constant block (like Clarity) will read this.
        // Shaders that don't (like CAS/FXAA) will safely ignore it, provided pushConstantSize matches the layout.
        if (pushConstantSize > 0) {
            std::vector<float> pushData(pushConstantSize / sizeof(float), 0.0f);
            
            // Populate texelSize (X and Y)
            if (pushData.size() >= 2) {
                pushData[0] = 1.0f / static_cast<float>(imageExtent.width); 
                pushData[1] = 1.0f / static_cast<float>(imageExtent.height);
            }
            
            // If the layout expects pixelSize at offset 4 (like Clarity effects), populate it
            if (pushData.size() >= 6) {
                pushData[4] = pushData[0]; // pixelSizeX
                pushData[5] = pushData[1]; // pixelSizeY
            }

            pLogicalDevice->vkd.CmdPushConstants(
                commandBuffer, 
                pipelineLayout,              
                VK_SHADER_STAGE_FRAGMENT_BIT, 
                0,                           
                pushConstantSize,           
                pushData.data()                    
            );
        }

        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        Logger::debug("after draw");

        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);
        Logger::debug("after end renderpass");

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                                               0, 0, nullptr, 0, nullptr, 1, &secondBarrier);
        Logger::debug("after the second pipeline barrier");
    }
    
    SimpleEffect::~SimpleEffect()
    {
        Logger::debug("destroying SimpleEffect " + convertToString(this));
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, graphicsPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, vertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, fragmentModule, nullptr);

        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);
        for (unsigned int i = 0; i < framebuffers.size(); i++)
        {
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, framebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputImageViews[i], nullptr);
        }
        Logger::debug("after DestroyImageView");
        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
    }
} // namespace vkBasalt
