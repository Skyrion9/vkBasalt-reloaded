#include "effect_transfer.hpp"
#include "config.hpp"
#include "logical_device.hpp"
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vkBasalt
{
    TransferEffect::TransferEffect(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D imageExtent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig) :
        pLogicalDevice(pLogicalDevice), inputImages(inputImages), outputImages(outputImages), imageExtent(imageExtent)
    {
    }

    void TransferEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        VkImageCopy imageCopy    = {};
        imageCopy.srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
        imageCopy.srcOffset      = {.x = 0, .y = 0, .z = 0};
        imageCopy.dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
        imageCopy.dstOffset = {.x = 0, .y = 0, .z = 0};
        imageCopy.extent    = {.width = imageExtent.width, .height = imageExtent.height, .depth = 1};

        VkImageMemoryBarrier barrier = {};
        barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange     = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1};

        // Barrier 1: Input -> TRANSFER_SRC
        barrier.image         = inputImages[imageIndex];
        barrier.srcAccessMask = isFirstInChain ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                               : (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            isFirstInChain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                           : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Barrier 2: Output UNDEFINED -> TRANSFER_DST
        barrier.image         = outputImages[imageIndex];
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &barrier);

        // Execute Copy
        pLogicalDevice->vkd.CmdCopyImage(
            commandBuffer, inputImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, outputImages[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);

        // Barrier 3: Output TRANSFER_DST -> Final Layout
        barrier.image         = outputImages[imageIndex];
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = isLastInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkPipelineStageFlags dstStage3 =
            isLastInChain ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                          : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                             | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier.dstAccessMask = isLastInChain ? VK_ACCESS_MEMORY_READ_BIT
                                              : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage3, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Barrier 4: Input TRANSFER_SRC -> Restore original layout
        barrier.image         = inputImages[imageIndex];
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = isFirstInChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &barrier);
    }

    TransferEffect::~TransferEffect() = default;

} // namespace vkBasalt
