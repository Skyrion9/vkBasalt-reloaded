#include "screenshot.hpp"
#include "format.hpp"
#include "logger.hpp"
#include "game_detect.hpp"
#include "color_math.hpp"

#include <stb_image_write.h>
#include "tinyexr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace vkBasalt {
    std::atomic<bool> g_triggerScreenshot{false};

    // one at a time is sufficient
    struct PendingScreenshot {
        bool active = false;
        LogicalDevice* device = nullptr;
        VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkBuffer stagingBufferBefore = VK_NULL_HANDLE;
        VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
        VkDeviceMemory bufferMemoryBefore = VK_NULL_HANDLE;
        VkDeviceSize bufferSize = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool saveBefore = false;
        std::string outputDir;
        std::string format;
        int quality = 95;
        VkFormat vkFormatAfter = VK_FORMAT_UNDEFINED;
        ColorSpaceMode csmAfter = ColorSpaceMode::SDR_SRGB;
        VkFormat vkFormatBefore = VK_FORMAT_UNDEFINED;
        ColorSpaceMode csmBefore = ColorSpaceMode::SDR_SRGB;
    };

    static PendingScreenshot g_pending;

    bool hasPendingScreenshot() {
        return g_pending.active;
    }

    // Helpers
    static std::string formatToExtension(const std::string& format) {
        if (format == "jpg" || format == "jpeg") return ".jpg";
        if (format == "bmp") return ".bmp";
        if (format == "tga") return ".tga";
        if (format == "hdr") return ".hdr";
        if (format == "exr") return ".exr";
        return ".png";
    }

    static std::string generateScreenshotPath(const std::string& baseDir, const std::string& suffix, const std::string& format) {
        static std::string gameId = computeGameId();
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", t);
        std::string filename = std::string(timeBuf) + suffix + "_" + gameId + formatToExtension(format);
        return (std::filesystem::path(baseDir) / filename).string();
    }

    static bool writeImage(const std::string& path, int width, int height,
                           const std::vector<uint8_t>& pixels, const std::string& format, int quality,
                           VkFormat vkFormat, ColorSpaceMode csm) {

        size_t pixelCount = (size_t)width * height;
        int dstChannels = 3;
        bool wantHDR = (format == "hdr" || format == "exr");
        
        std::vector<float> linearPixels;
        std::vector<uint8_t> rgbPixels;
        
        if (wantHDR) {
            linearPixels.resize(pixelCount * dstChannels);
        } else {
            rgbPixels.resize(pixelCount * dstChannels);
        }

        // Unified pixel unpacker: always decodes to linear light
        auto unpackPixel = [&](size_t i, float& r, float& g, float& b) {
            switch (vkFormat) {
                case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
                    const uint32_t* packed = reinterpret_cast<const uint32_t*>(pixels.data());
                    bool isA2R10 = (vkFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32);
                    uint32_t p = packed[i];
                    r = isA2R10 ? ((p >> 20) & 0x3FF) / 1023.0f : ((p >>  0) & 0x3FF) / 1023.0f;
                    g = ((p >> 10) & 0x3FF) / 1023.0f;
                    b = isA2R10 ? ((p >>  0) & 0x3FF) / 1023.0f : ((p >> 20) & 0x3FF) / 1023.0f;
                    r = decodeColor(r, csm);
                    g = decodeColor(g, csm);
                    b = decodeColor(b, csm);
                    break;
                }
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                case VK_FORMAT_R16G16B16_SFLOAT:
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                case VK_FORMAT_R32G32B32_SFLOAT: {
                    bool isFP32 = (vkFormat == VK_FORMAT_R32G32B32A32_SFLOAT || vkFormat == VK_FORMAT_R32G32B32_SFLOAT);
                    uint32_t channels = getBytesPerPixel(vkFormat) / (isFP32 ? 4 : 2);
                    if (isFP32) {
                        const float* fpixels = reinterpret_cast<const float*>(pixels.data());
                        r = fpixels[i * channels + 0];
                        g = fpixels[i * channels + 1];
                        b = fpixels[i * channels + 2];
                    } else {
                        const uint16_t* fpixels = reinterpret_cast<const uint16_t*>(pixels.data());
                        r = halfToFloat(fpixels[i * channels + 0]);
                        g = halfToFloat(fpixels[i * channels + 1]);
                        b = halfToFloat(fpixels[i * channels + 2]);
                    }
                    r = decodeColor(r, csm);
                    g = decodeColor(g, csm);
                    b = decodeColor(b, csm);
                    break;
                }
                default: {
                    bool isBGR = isBGRFormat(vkFormat);
                    uint32_t stride = getBytesPerPixel(vkFormat);
                    uint8_t r8, g8, b8;
                    if (isBGR) {
                        r8 = pixels[i * stride + 2];
                        g8 = pixels[i * stride + 1];
                        b8 = pixels[i * stride + 0];
                    } else {
                        r8 = pixels[i * stride + 0];
                        g8 = pixels[i * stride + 1];
                        b8 = pixels[i * stride + 2];
                    }
                    r = decodeColor(r8 / 255.0f, csm);
                    g = decodeColor(g8 / 255.0f, csm);
                    b = decodeColor(b8 / 255.0f, csm);
                    break;
                }
            }
        };

        if (wantHDR) {
            for (size_t i = 0; i < pixelCount; i++) {
                float r, g, b;
                unpackPixel(i, r, g, b);
                // .hdr/.exr expect linear light. Clamp negatives to 0.
                linearPixels[i * 3 + 0] = std::max(0.0f, r);
                linearPixels[i * 3 + 1] = std::max(0.0f, g);
                linearPixels[i * 3 + 2] = std::max(0.0f, b);
            }
        } else {
            // 8-bit formats (PNG, JPG, BMP, TGA) expect sRGB gamma-encoded values in [0, 1].
            bool isHDRSource = (csm != ColorSpaceMode::SDR_SRGB && csm != ColorSpaceMode::DISPLAY_P3_NONLINEAR);
            for (size_t i = 0; i < pixelCount; i++) {
                float r, g, b;
                unpackPixel(i, r, g, b);
                
                if (isHDRSource) {
                    // Tonemap to compress unbounded HDR range into displayable [0,1]. Using ACES (Narkowicz fit) instead of Reinhard for better highlight roll off and contrast.
                    r = tonemapACES(r);
                    g = tonemapACES(g);
                    b = tonemapACES(b);
                }
                
                // Convert linear light to sRGB gamma for the 8-bit file
                r = linearToSrgb(r);
                g = linearToSrgb(g);
                b = linearToSrgb(b);
                
                rgbPixels[i * 3 + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                rgbPixels[i * 3 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                rgbPixels[i * 3 + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
            }
        }

        if (format == "jpg" || format == "jpeg") {
            return stbi_write_jpg(path.c_str(), width, height, dstChannels, rgbPixels.data(), quality) != 0;
        } else if (format == "bmp") {
            return stbi_write_bmp(path.c_str(), width, height, dstChannels, rgbPixels.data()) != 0;
        } else if (format == "tga") {
            return stbi_write_tga(path.c_str(), width, height, dstChannels, rgbPixels.data()) != 0;
        } else if (format == "hdr") {
            return stbi_write_hdr(path.c_str(), width, height, dstChannels, linearPixels.data()) != 0;
        } else if (format == "exr") {
            const char* err = nullptr;
            // save_as_fp16 = 1 ensures true 16 bit half-float HDR preservation
            int ret = SaveEXR(linearPixels.data(), width, height, dstChannels, 1, path.c_str(), &err);
            if (ret != TINYEXR_SUCCESS) {
                if (err) {
                    Logger::err(std::string("EXR write failed: ") + err);
                    FreeEXRErrorMessage(err);
                }
                return false;
            }
            return true;
        }
        return stbi_write_png(path.c_str(), width, height, dstChannels, rgbPixels.data(), width * dstChannels) != 0;
    }

    static uint32_t findHostVisibleMemoryType(LogicalDevice* pDevice, uint32_t typeBits) {
        for (uint32_t i = 0; i < pDevice->memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & (1 << i)) &&
                (pDevice->memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                return i;
            }
        }
        return 0;
    }

    // captureScreenshot: submit GPU copy, return immediately
    void captureScreenshot(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain,
                           uint32_t imageIndex, bool saveBeforeAfter,
                           const std::string& outputPath,
                           const std::string& format, int quality,
                           ColorSpaceMode csm) {
        if (!pDevice || !pSwapchain) return;
        if (g_pending.active) {
            Logger::warn("Screenshot already in progress, skipping.");
            return;
        }

        VkExtent2D extent = pSwapchain->imageExtent;
        VkFormat vkFormat = pSwapchain->format;

        uint32_t bytesPerPixel = getBytesPerPixel(vkFormat);
        VkDeviceSize bufferSize = (VkDeviceSize)extent.width * extent.height * bytesPerPixel;

        std::string dir = outputPath;
        if (dir.empty()) {
            const char* home = getenv("HOME");
            if (home) {
                dir = std::string(home) + "/Pictures/vkBasalt-reloaded";
            } else {
                dir = ".";
            }
        }
        std::filesystem::create_directories(dir);

        // Create staging buffers
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (pDevice->vkd.CreateBuffer(pDevice->device, &bufferInfo, nullptr, &g_pending.stagingBuffer) != VK_SUCCESS) {
            Logger::err("Screenshot: Failed to create staging buffer");
            return;
        }

        // Allocate memory for "after" buffer
        VkMemoryRequirements memReqs;
        pDevice->vkd.GetBufferMemoryRequirements(pDevice->device, g_pending.stagingBuffer, &memReqs);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findHostVisibleMemoryType(pDevice, memReqs.memoryTypeBits);

        if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &g_pending.bufferMemory) != VK_SUCCESS) {
            pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBuffer, nullptr);
            Logger::err("Screenshot: Failed to allocate staging buffer memory");
            return;
        }
        pDevice->vkd.BindBufferMemory(pDevice->device, g_pending.stagingBuffer, g_pending.bufferMemory, 0);

        // Create before staging buffer if needed. If fakeImages is empty (passthrough mode), the game renders directly to real images.
        // We use the real image as the "before" source, but skip the copy if it's identical to the "after" image.
        bool needBefore = saveBeforeAfter;
        VkImage beforeImage = VK_NULL_HANDLE;
        if (!pSwapchain->fakeImages.empty() && imageIndex < pSwapchain->fakeImages.size()) {
            beforeImage = pSwapchain->fakeImages[imageIndex];
        } else {
            beforeImage = pSwapchain->images[imageIndex];
        }
        
        // If before and after are the exact same image, skip the redundant GPU copy
        if (needBefore && beforeImage == pSwapchain->images[imageIndex]) {
            needBefore = false;
        }

        if (needBefore) {
            if (pDevice->vkd.CreateBuffer(pDevice->device, &bufferInfo, nullptr, &g_pending.stagingBufferBefore) == VK_SUCCESS) {
                VkMemoryRequirements memReqsBefore;
                pDevice->vkd.GetBufferMemoryRequirements(pDevice->device, g_pending.stagingBufferBefore, &memReqsBefore);
                allocInfo.allocationSize = memReqsBefore.size;
                allocInfo.memoryTypeIndex = findHostVisibleMemoryType(pDevice, memReqsBefore.memoryTypeBits);
                if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &g_pending.bufferMemoryBefore) == VK_SUCCESS) {
                    pDevice->vkd.BindBufferMemory(pDevice->device, g_pending.stagingBufferBefore, g_pending.bufferMemoryBefore, 0);
                } else {
                    pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBufferBefore, nullptr);
                    g_pending.stagingBufferBefore = VK_NULL_HANDLE;
                    needBefore = false;
                }
            } else {
                needBefore = false;
            }
        }

        // Record command buffer
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

        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {extent.width, extent.height, 1};

        // Copy after image (post processed)
        {
            VkImageMemoryBarrier toTransfer = {};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toTransfer.image = pSwapchain->images[imageIndex];
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
            pDevice->vkd.CmdCopyImageToBuffer(cmdBuf, pSwapchain->images[imageIndex],
                                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_pending.stagingBuffer, 1, &region);
            VkImageMemoryBarrier toPresent = {};
            toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.image = pSwapchain->images[imageIndex];
            toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
        }

        // Copy before image (game output) if requested
        if (needBefore) {
            VkImageMemoryBarrier toTransfer = {};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toTransfer.image = beforeImage;
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
            pDevice->vkd.CmdCopyImageToBuffer(cmdBuf, beforeImage,
                                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_pending.stagingBufferBefore, 1, &region);
            VkImageMemoryBarrier toPresent = {};
            toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.image = beforeImage;
            toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
        }

        pDevice->vkd.EndCommandBuffer(cmdBuf);

        // Create fence
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        pDevice->vkd.CreateFence(pDevice->device, &fenceInfo, nullptr, &g_pending.fence);

        // Submit and return immediately (no wait)
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        pDevice->vkd.QueueSubmit(pDevice->queue, 1, &submitInfo, g_pending.fence);

        g_pending.cmdBuf = cmdBuf;

        // Fill pending state
        g_pending.device = pDevice;
        g_pending.bufferSize = bufferSize;
        g_pending.width = extent.width;
        g_pending.height = extent.height;
        g_pending.saveBefore = needBefore;
        g_pending.outputDir = dir;
        g_pending.format = format;
        g_pending.quality = quality;
        
        // Store separate formats/colorspaces for before/after because the effect chain may convert SDR -> HDR
        g_pending.vkFormatAfter = pSwapchain->format;
        g_pending.csmAfter = getColorSpaceMode(pSwapchain->format, pSwapchain->colorSpace);
        g_pending.vkFormatBefore = pSwapchain->sourceFormat;
        g_pending.csmBefore = getColorSpaceMode(pSwapchain->sourceFormat, pSwapchain->sourceColorSpace);
        
        g_pending.active = true;

        Logger::debug("Screenshot capture submitted (async).");
    }

    // checks fence, write file
    bool processPendingScreenshot() {
        if (!g_pending.active) return false;

        LogicalDevice* pDevice = g_pending.device;

        // Check if GPU work is complete (non blocking poll with no timeout)
        VkResult fenceStatus = pDevice->vkd.GetFenceStatus(pDevice->device, g_pending.fence);
        if (fenceStatus != VK_SUCCESS) {
            return false; // GPU still working, try again next frame
        }

        // GPU is done — read back and write files
        bool wroteSomething = false;

        // Write after screenshot
        {
            void* mapped = nullptr;
            if (pDevice->vkd.MapMemory(pDevice->device, g_pending.bufferMemory, 0, g_pending.bufferSize, 0, &mapped) == VK_SUCCESS) {
                std::vector<uint8_t> pixels(g_pending.bufferSize);
                std::memcpy(pixels.data(), mapped, g_pending.bufferSize);
                pDevice->vkd.UnmapMemory(pDevice->device, g_pending.bufferMemory);

                std::string path = generateScreenshotPath(g_pending.outputDir, "_after", g_pending.format);
                if (writeImage(path, g_pending.width, g_pending.height, pixels, g_pending.format, g_pending.quality, g_pending.vkFormatAfter, g_pending.csmAfter)) {
                    Logger::info("Screenshot saved: " + path);
                    wroteSomething = true;
                } else {
                    Logger::err("Failed to write screenshot: " + path);
                }
            }
        }

        // Write before screenshot
        if (g_pending.saveBefore && g_pending.stagingBufferBefore != VK_NULL_HANDLE) {
            void* mapped = nullptr;
            if (pDevice->vkd.MapMemory(pDevice->device, g_pending.bufferMemoryBefore, 0, g_pending.bufferSize, 0, &mapped) == VK_SUCCESS) {
                std::vector<uint8_t> pixels(g_pending.bufferSize);
                std::memcpy(pixels.data(), mapped, g_pending.bufferSize);
                pDevice->vkd.UnmapMemory(pDevice->device, g_pending.bufferMemoryBefore);

                std::string path = generateScreenshotPath(g_pending.outputDir, "_before", g_pending.format);
                if (writeImage(path, g_pending.width, g_pending.height, pixels, g_pending.format, g_pending.quality, g_pending.vkFormatBefore, g_pending.csmBefore)) {
                    Logger::info("Before screenshot saved: " + path);
                    wroteSomething = true;
                } else {
                    Logger::err("Failed to write before screenshot: " + path);
                }
            }
        }

        // Cleanup all GPU resources
        pDevice->vkd.FreeCommandBuffers(pDevice->device, pDevice->commandPool, 1, &g_pending.cmdBuf);
        pDevice->vkd.DestroyFence(pDevice->device, g_pending.fence, nullptr);
        pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBuffer, nullptr);
        pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemory, nullptr);
        if (g_pending.stagingBufferBefore != VK_NULL_HANDLE) {
            pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBufferBefore, nullptr);
            pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemoryBefore, nullptr);
        }

        // Reset pending state
        g_pending = PendingScreenshot{};

        return wroteSomething;
    }
} // namespace vkBasalt
