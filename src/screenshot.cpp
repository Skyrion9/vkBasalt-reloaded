#include "screenshot.hpp"
#include "logger.hpp"
#include "game_detect.hpp"

#include <stb_image_write.h>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vkBasalt {

std::atomic<bool> g_triggerScreenshot{false};

    static std::string generateScreenshotPath(const std::string& baseDir, const std::string& suffix) {
        static std::string gameId = computeGameId();
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", t);
        std::string filename = std::string(timeBuf) + suffix + "_" + gameId + ".png";
        return (std::filesystem::path(baseDir) / filename).string();
    }

    static bool copyImageToBuffer(LogicalDevice* pDevice, VkImage image, VkExtent2D extent,
                                VkFormat format, std::vector<uint8_t>& pixels) {
        // Determine bytes per pixel
        uint32_t bytesPerPixel = 4; // Assume RGBA8 for swapchain images
        VkDeviceSize bufferSize = (VkDeviceSize)extent.width * extent.height * bytesPerPixel;

        // Create staging buffer
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        if (pDevice->vkd.CreateBuffer(pDevice->device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            Logger::err("Screenshot: Failed to create staging buffer");
            return false;
        }

        // Allocate memory for the buffer
        VkMemoryRequirements memReqs;
        pDevice->vkd.GetBufferMemoryRequirements(pDevice->device, stagingBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;

        // Find host-visible memory type
        uint32_t memTypeIndex = 0;
        for (uint32_t i = 0; i < pDevice->memoryProperties.memoryTypeCount; i++) {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (pDevice->memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                memTypeIndex = i;
                break;
            }
        }
        allocInfo.memoryTypeIndex = memTypeIndex;

        VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
        if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            pDevice->vkd.DestroyBuffer(pDevice->device, stagingBuffer, nullptr);
            Logger::err("Screenshot: Failed to allocate staging buffer memory");
            return false;
        }
        pDevice->vkd.BindBufferMemory(pDevice->device, stagingBuffer, bufferMemory, 0);

        // Create command buffer for the copy
        VkCommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = pDevice->commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
        pDevice->vkd.AllocateCommandBuffers(pDevice->device, &cmdAllocInfo, &cmdBuf);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pDevice->vkd.BeginCommandBuffer(cmdBuf, &beginInfo);

        // Transition image to TRANSFER_SRC_OPTIMAL
        VkImageMemoryBarrier toTransfer = {};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.image = image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        // Copy image to buffer
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {extent.width, extent.height, 1};
        pDevice->vkd.CmdCopyImageToBuffer(cmdBuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        stagingBuffer, 1, &region);

        // Transition back to PRESENT_SRC_OPTIMAL
        VkImageMemoryBarrier toPresent = {};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.image = image;
        toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

        pDevice->vkd.EndCommandBuffer(cmdBuf);

        // Create fence for synchronization
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        pDevice->vkd.CreateFence(pDevice->device, &fenceInfo, nullptr, &fence);

        // Submit and wait
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        pDevice->vkd.QueueSubmit(pDevice->queue, 1, &submitInfo, fence);
        pDevice->vkd.WaitForFences(pDevice->device, 1, &fence, VK_TRUE, 5000000000ULL); // 5s timeout

        // Read back pixels
        void* mapped = nullptr;
        if (pDevice->vkd.MapMemory(pDevice->device, bufferMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS) {
            pixels.resize(bufferSize);
            std::memcpy(pixels.data(), mapped, bufferSize);
            pDevice->vkd.UnmapMemory(pDevice->device, bufferMemory);
        }

        // Cleanup
        pDevice->vkd.DestroyFence(pDevice->device, fence, nullptr);
        pDevice->vkd.FreeCommandBuffers(pDevice->device, pDevice->commandPool, 1, &cmdBuf);
        pDevice->vkd.DestroyBuffer(pDevice->device, stagingBuffer, nullptr);
        pDevice->vkd.FreeMemory(pDevice->device, bufferMemory, nullptr);

        return !pixels.empty();
    }

    void captureScreenshot(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                        uint32_t imageIndex, bool saveBeforeAfter,
                        const std::string& outputPath) {
        if (!pDevice || !pSwapchain) return;

        VkExtent2D extent = pSwapchain->imageExtent;
        uint32_t bytesPerPixel = 4;

        // Determine output directory
        std::string dir = outputPath.empty() ? std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".").string()
                                            : outputPath;
        std::filesystem::create_directories(dir);

        // Save "after" (post-processed) screenshot
        {
            std::vector<uint8_t> pixels;
            if (copyImageToBuffer(pDevice, pSwapchain->images[imageIndex], extent, pSwapchain->format, pixels)) {
                std::string path = generateScreenshotPath(dir, "_after");
                if (stbi_write_png(path.c_str(), extent.width, extent.height, bytesPerPixel,
                                pixels.data(), extent.width * bytesPerPixel)) {
                    Logger::info("Screenshot saved: " + path);
                } else {
                    Logger::err("Failed to write screenshot: " + path);
                }
            }
        }

        // Save "before" (game output, pre-effects) screenshot if enabled
        if (saveBeforeAfter && !pSwapchain->fakeImages.empty()) {
            std::vector<uint8_t> pixels;
            // fakeImages[0..imageCount-1] is slice 0 (game's render target)
            if (imageIndex < pSwapchain->fakeImages.size()) {
                if (copyImageToBuffer(pDevice, pSwapchain->fakeImages[imageIndex], extent, pSwapchain->format, pixels)) {
                    std::string path = generateScreenshotPath(dir, "_before");
                    if (stbi_write_png(path.c_str(), extent.width, extent.height, bytesPerPixel,
                                    pixels.data(), extent.width * bytesPerPixel)) {
                        Logger::info("Before screenshot saved: " + path);
                    } else {
                        Logger::err("Failed to write before screenshot: " + path);
                    }
                }
            }
        }
    }

} // namespace vkBasalt
