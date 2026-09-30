#include "screenshot.hpp"

#include "color_math.hpp"
#include "format.hpp"
#include "game_detect.hpp"
#include "logger.hpp"

#include <stb_image_write.h>
#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace vkBasalt
{
    std::atomic<bool> g_triggerScreenshot{false};

    namespace
    {
        // one at a time is sufficient for screenshots
        struct PendingScreenshot
        {
            bool active                       = false;
            LogicalDevice* device             = nullptr;
            VkCommandBuffer cmdBuf            = VK_NULL_HANDLE;
            VkFence fence                     = VK_NULL_HANDLE;
            VkBuffer stagingBuffer            = VK_NULL_HANDLE;
            VkBuffer stagingBufferBefore      = VK_NULL_HANDLE;
            VkDeviceMemory bufferMemory       = VK_NULL_HANDLE;
            VkDeviceMemory bufferMemoryBefore = VK_NULL_HANDLE;
            VkDeviceSize bufferSize           = 0;
            uint32_t width                    = 0;
            uint32_t height                   = 0;
            bool saveBefore                   = false;
            std::string outputDir;
            std::string format;
            int quality              = 95;
            float sdrWhitePointNits  = 203.0f;
            VkFormat vkFormatAfter   = VK_FORMAT_UNDEFINED;
            ColorSpaceMode csmAfter  = ColorSpaceMode::SDR_SRGB;
            VkFormat vkFormatBefore  = VK_FORMAT_UNDEFINED;
            ColorSpaceMode csmBefore = ColorSpaceMode::SDR_SRGB;
        };

        PendingScreenshot g_pending;

        struct ScreenshotWriteTask
        {
            std::string path;
            std::vector<uint8_t> pixels;
            std::string format;
            int quality;
            int width;
            int height;
            VkFormat vkFormat;
            ColorSpaceMode csm;
            float sdrWhitePointNits = 203.0f;
        };

        std::string formatToExtension(const std::string& format)
        {
            if (format == "jpg" || format == "jpeg") return ".jpg";
            if (format == "bmp") return ".bmp";
            if (format == "tga") return ".tga";
            if (format == "hdr") return ".hdr";
            if (format == "exr") return ".exr";
            return ".png";
        }

        std::string
        generateScreenshotPath(const std::string& baseDir, const std::string& suffix, const std::string& format)
        {
            static const std::string gameId = computeGameId();

            std::time_t now = std::time(nullptr);
            std::tm t{};
            localtime_r(&now, &t);

            char timeBuf[64];
            std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &t);

            std::string filename = std::string(timeBuf) + suffix + "_" + gameId + formatToExtension(format);
            return (std::filesystem::path(baseDir) / filename).string();
        }

        uint32_t findHostVisibleMemoryType(LogicalDevice* pDevice, uint32_t typeBits)
        {
            for (uint32_t i = 0; i < pDevice->memoryProperties.memoryTypeCount; i++) {
                if ((typeBits & (1 << i))
                    && (pDevice->memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                    return i;
                }
            }
            return 0;
        }

        bool writeImage(
            const std::string& path,
            int width,
            int height,
            const std::vector<uint8_t>& pixels,
            const std::string& format,
            int quality,
            VkFormat vkFormat,
            ColorSpaceMode csm,
            float sdrWhitePointNits)
        {
            size_t pixelCount         = static_cast<size_t>(width) * height;
            constexpr int dstChannels = 3;
            bool wantHDR              = (format == "hdr" || format == "exr");

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
                        const auto* packed = reinterpret_cast<const uint32_t*>(pixels.data());
                        bool isA2R10       = (vkFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32);
                        uint32_t p         = packed[i];
                        r                  = isA2R10 ? ((p >> 20) & 0x3FF) / 1023.0f : ((p >> 0) & 0x3FF) / 1023.0f;
                        g                  = ((p >> 10) & 0x3FF) / 1023.0f;
                        b                  = isA2R10 ? ((p >> 0) & 0x3FF) / 1023.0f : ((p >> 20) & 0x3FF) / 1023.0f;
                        r                  = decodeColor(r, csm);
                        g                  = decodeColor(g, csm);
                        b                  = decodeColor(b, csm);
                        break;
                    }
                    case VK_FORMAT_R16G16B16A16_SFLOAT:
                    case VK_FORMAT_R16G16B16_SFLOAT:
                    case VK_FORMAT_R32G32B32A32_SFLOAT:
                    case VK_FORMAT_R32G32B32_SFLOAT: {
                        bool isFP32 =
                            (vkFormat == VK_FORMAT_R32G32B32A32_SFLOAT || vkFormat == VK_FORMAT_R32G32B32_SFLOAT);
                        uint32_t channels = getBytesPerPixel(vkFormat) / (isFP32 ? 4 : 2);
                        if (isFP32) {
                            const auto* fpixels = reinterpret_cast<const float*>(pixels.data());
                            r                   = fpixels[i * channels + 0];
                            g                   = fpixels[i * channels + 1];
                            b                   = fpixels[i * channels + 2];
                        } else {
                            const auto* fpixels = reinterpret_cast<const uint16_t*>(pixels.data());
                            r                   = halfToFloat(fpixels[i * channels + 0]);
                            g                   = halfToFloat(fpixels[i * channels + 1]);
                            b                   = halfToFloat(fpixels[i * channels + 2]);
                        }
                        r = decodeColor(r, csm);
                        g = decodeColor(g, csm);
                        b = decodeColor(b, csm);
                        break;
                    }
                    default: {
                        bool isBGR      = isBGRFormat(vkFormat);
                        uint32_t stride = getBytesPerPixel(vkFormat);
                        uint8_t r8 = 0, g8 = 0, b8 = 0;
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
                    float r = NAN, g = NAN, b = NAN;
                    unpackPixel(i, r, g, b);
                    // .hdr/.exr expect linear light. Clamp negatives to 0.
                    linearPixels[i * 3 + 0] = std::max(0.0f, r);
                    linearPixels[i * 3 + 1] = std::max(0.0f, g);
                    linearPixels[i * 3 + 2] = std::max(0.0f, b);
                }
            } else {
                // 8-bit formats (PNG, JPG, BMP, TGA) expect sRGB gamma-encoded values in [0, 1] with Rec.709 primaries.
                bool isHDRSource         = isHDRTransferFunction(csm);
                ColorPrimaries primaries = getPrimaries(csm);

                for (size_t i = 0; i < pixelCount; i++) {
                    float r = NAN, g = NAN, b = NAN;
                    unpackPixel(i, r, g, b);

                    // Gamut map wide color spaces to Rec.709 before tonemapping/sRGB encoding
                    if (primaries == ColorPrimaries::Rec2020) {
                        rec2020ToRec709(r, g, b);
                    } else if (primaries == ColorPrimaries::DisplayP3) {
                        displayP3ToRec709(r, g, b);
                    }

                    if (isHDRSource) {
                        // Normalize by SDR white point so tonemapACES receives values where 1.0 = reference white. Our linear values use 1.0 = 100 nits.
                        float sdrWhiteLinear = sdrWhitePointNits * 0.01f;
                        r                    = tonemapACES(r / sdrWhiteLinear);
                        g                    = tonemapACES(g / sdrWhiteLinear);
                        b                    = tonemapACES(b / sdrWhiteLinear);
                    }

                    // Encode to sRGB gamma and pack into 8-bit RGB
                    r                    = linearToSrgb(r);
                    g                    = linearToSrgb(g);
                    b                    = linearToSrgb(b);
                    rgbPixels[i * 3 + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                    rgbPixels[i * 3 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                    rgbPixels[i * 3 + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
                }
            }

            // Format writers
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
                EXRHeader header;
                InitEXRHeader(&header);
                EXRImage image;
                InitEXRImage(&image);

                image.num_channels = 3;
                image.width        = width;
                image.height       = height;

                std::vector<float> channelR(pixelCount), channelG(pixelCount), channelB(pixelCount);
                for (size_t i = 0; i < pixelCount; i++) {
                    channelR[i] = linearPixels[i * 3 + 0];
                    channelG[i] = linearPixels[i * 3 + 1];
                    channelB[i] = linearPixels[i * 3 + 2];
                }

                float* image_ptr[3];
                image_ptr[0] = channelB.data();
                image_ptr[1] = channelG.data();
                image_ptr[2] = channelR.data();
                image.images = reinterpret_cast<unsigned char**>(image_ptr);

                header.num_channels = 3;

                std::vector<EXRChannelInfo> channels(3);
                std::snprintf(channels[0].name, 256, "B");
                std::snprintf(channels[1].name, 256, "G");
                std::snprintf(channels[2].name, 256, "R");
                header.channels = channels.data();

                std::vector<int> pixel_types(3, TINYEXR_PIXELTYPE_FLOAT);
                std::vector<int> requested_pixel_types(3, TINYEXR_PIXELTYPE_HALF);
                header.pixel_types           = pixel_types.data();
                header.requested_pixel_types = requested_pixel_types.data();

                float chromaticities[8];
                ColorPrimaries primaries = getPrimaries(csm);

                if (primaries == ColorPrimaries::Rec2020) {
                    chromaticities[0] = 0.708f;
                    chromaticities[1] = 0.292f; // Red
                    chromaticities[2] = 0.170f;
                    chromaticities[3] = 0.797f; // Green
                    chromaticities[4] = 0.131f;
                    chromaticities[5] = 0.046f; // Blue
                } else if (primaries == ColorPrimaries::DisplayP3) {
                    chromaticities[0] = 0.680f;
                    chromaticities[1] = 0.320f; // Red
                    chromaticities[2] = 0.265f;
                    chromaticities[3] = 0.690f; // Green
                    chromaticities[4] = 0.150f;
                    chromaticities[5] = 0.060f; // Blue
                } else {
                    chromaticities[0] = 0.6400f;
                    chromaticities[1] = 0.3300f; // Red
                    chromaticities[2] = 0.3000f;
                    chromaticities[3] = 0.6000f; // Green
                    chromaticities[4] = 0.1500f;
                    chromaticities[5] = 0.0600f; // Blue
                }
                chromaticities[6] = 0.3127f;
                chromaticities[7] = 0.3290f; // White (D65)

                std::vector<EXRAttribute> custom_attributes(1);
                std::snprintf(custom_attributes[0].name, 256, "chromaticities");
                std::snprintf(custom_attributes[0].type, 256, "chromaticities");
                custom_attributes[0].value = reinterpret_cast<unsigned char*>(chromaticities);
                custom_attributes[0].size  = sizeof(chromaticities);

                header.num_custom_attributes = 1;
                header.custom_attributes     = custom_attributes.data();

                int ret = SaveEXRImageToFile(&image, &header, path.c_str(), &err);

                if (ret != TINYEXR_SUCCESS) {
                    if (err) {
                        Logger::err(std::string("EXR write failed: ") + err);
                        FreeEXRErrorMessage(err);
                    }
                    return false;
                }
                return true;
            }

            // Default to PNG
            return stbi_write_png(path.c_str(), width, height, dstChannels, rgbPixels.data(), width * dstChannels) != 0;
        }

        void writeScreenshotsInBackground(std::vector<ScreenshotWriteTask> tasks)
        {
            for (auto& task : tasks) {
                if (writeImage(
                        task.path, task.width, task.height, task.pixels, task.format, task.quality, task.vkFormat,
                        task.csm, task.sdrWhitePointNits)) {
                    Logger::info("Screenshot saved: " + task.path);
                } else {
                    Logger::err("Failed to write screenshot: " + task.path);
                }
            }
        }

    } // namespace

    bool hasPendingScreenshot()
    {
        return g_pending.active;
    }

    void captureScreenshot(
        LogicalDevice* pDevice,
        LogicalSwapchain* pSwapchain,
        uint32_t imageIndex,
        bool saveBeforeAfter,
        const std::string& outputPath,
        const std::string& format,
        int quality,
        float sdrWhitePointNits)
    {
        if (!pDevice || !pSwapchain) return;
        if (g_pending.active) {
            Logger::warn("Screenshot already in progress, skipping.");
            return;
        }

        VkExtent2D extent       = pSwapchain->imageExtent;
        VkFormat vkFormat       = pSwapchain->format;
        uint32_t bytesPerPixel  = getBytesPerPixel(vkFormat);
        VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * bytesPerPixel;

        std::string dir = outputPath;
        if (dir.empty()) {
            const char* home = std::getenv("HOME");
            if (home) {
                dir = std::string(home) + "/Pictures/vkBasalt-reloaded";
            } else {
                dir = ".";
            }
        }
        std::filesystem::create_directories(dir);

        // Helper to cleanup allocated resources on failure
        auto cleanupPending = [&]() {
            if (g_pending.stagingBuffer != VK_NULL_HANDLE) {
                pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBuffer, nullptr);
                g_pending.stagingBuffer = VK_NULL_HANDLE;
            }
            if (g_pending.bufferMemory != VK_NULL_HANDLE) {
                pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemory, nullptr);
                g_pending.bufferMemory = VK_NULL_HANDLE;
            }
            if (g_pending.stagingBufferBefore != VK_NULL_HANDLE) {
                pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBufferBefore, nullptr);
                g_pending.stagingBufferBefore = VK_NULL_HANDLE;
            }
            if (g_pending.bufferMemoryBefore != VK_NULL_HANDLE) {
                pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemoryBefore, nullptr);
                g_pending.bufferMemoryBefore = VK_NULL_HANDLE;
            }
        };

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size               = bufferSize;
        bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        if (pDevice->vkd.CreateBuffer(pDevice->device, &bufferInfo, nullptr, &g_pending.stagingBuffer) != VK_SUCCESS) {
            Logger::err("Screenshot: Failed to create staging buffer");
            return;
        }

        VkMemoryRequirements memReqs;
        pDevice->vkd.GetBufferMemoryRequirements(pDevice->device, g_pending.stagingBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize       = memReqs.size;
        allocInfo.memoryTypeIndex      = findHostVisibleMemoryType(pDevice, memReqs.memoryTypeBits);

        if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &g_pending.bufferMemory) != VK_SUCCESS) {
            Logger::err("Screenshot: Failed to allocate staging buffer memory");
            cleanupPending();
            return;
        }
        pDevice->vkd.BindBufferMemory(pDevice->device, g_pending.stagingBuffer, g_pending.bufferMemory, 0);

        bool needBefore     = saveBeforeAfter;
        VkImage beforeImage = VK_NULL_HANDLE;
        if (!pSwapchain->fakeImages.empty() && imageIndex < pSwapchain->fakeImages.size()) {
            beforeImage = pSwapchain->fakeImages[imageIndex];
        } else {
            beforeImage = pSwapchain->images[imageIndex];
        }

        if (needBefore && beforeImage == pSwapchain->images[imageIndex]) {
            needBefore = false;
        }

        if (needBefore) {
            if (pDevice->vkd.CreateBuffer(pDevice->device, &bufferInfo, nullptr, &g_pending.stagingBufferBefore)
                == VK_SUCCESS) {
                VkMemoryRequirements memReqsBefore;
                pDevice->vkd.GetBufferMemoryRequirements(
                    pDevice->device, g_pending.stagingBufferBefore, &memReqsBefore);
                allocInfo.allocationSize  = memReqsBefore.size;
                allocInfo.memoryTypeIndex = findHostVisibleMemoryType(pDevice, memReqsBefore.memoryTypeBits);

                if (pDevice->vkd.AllocateMemory(pDevice->device, &allocInfo, nullptr, &g_pending.bufferMemoryBefore)
                    == VK_SUCCESS) {
                    pDevice->vkd.BindBufferMemory(
                        pDevice->device, g_pending.stagingBufferBefore, g_pending.bufferMemoryBefore, 0);
                } else {
                    pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBufferBefore, nullptr);
                    g_pending.stagingBufferBefore = VK_NULL_HANDLE;
                    needBefore                    = false;
                }
            } else {
                needBefore = false;
            }
        }

        // Record command buffer
        VkCommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool                 = pDevice->commandPool;
        cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount          = 1;

        VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
        if (pDevice->vkd.AllocateCommandBuffers(pDevice->device, &cmdAllocInfo, &cmdBuf) != VK_SUCCESS
            || cmdBuf == VK_NULL_HANDLE) {
            Logger::err("Screenshot: Failed to allocate command buffer");
            cleanupPending();
            return;
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pDevice->vkd.BeginCommandBuffer(cmdBuf, &beginInfo);

        VkBufferImageCopy region = {};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
        region.imageOffset = {.x = 0, .y = 0, .z = 0};
        region.imageExtent = {.width = extent.width, .height = extent.height, .depth = 1};

        // Copy after image (post processed)
        auto recordCopy = [&](VkImage srcImage, VkBuffer dstBuffer) {
            VkImageMemoryBarrier toTransfer = {};
            toTransfer.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
            toTransfer.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
            toTransfer.oldLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toTransfer.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toTransfer.image            = srcImage;
            toTransfer.subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pDevice->vkd.CmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                1, &toTransfer);
            pDevice->vkd.CmdCopyImageToBuffer(
                cmdBuf, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);
            VkImageMemoryBarrier toPresent = {};
            toPresent.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toPresent.srcAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
            toPresent.dstAccessMask        = VK_ACCESS_MEMORY_READ_BIT;
            toPresent.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toPresent.newLayout            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.image                = srcImage;
            toPresent.subresourceRange     = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1};
            pDevice->vkd.CmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                1, &toPresent);
        };

        recordCopy(pSwapchain->images[imageIndex], g_pending.stagingBuffer);
        if (needBefore) {
            recordCopy(beforeImage, g_pending.stagingBufferBefore);
        }

        pDevice->vkd.EndCommandBuffer(cmdBuf);

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (pDevice->vkd.CreateFence(pDevice->device, &fenceInfo, nullptr, &g_pending.fence) != VK_SUCCESS) {
            Logger::err("Screenshot: Failed to create fence");
            pDevice->vkd.FreeCommandBuffers(pDevice->device, pDevice->commandPool, 1, &cmdBuf);
            cleanupPending();
            return;
        }

        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmdBuf;
        pDevice->vkd.QueueSubmit(pDevice->queue, 1, &submitInfo, g_pending.fence);

        g_pending.cmdBuf = cmdBuf;

        g_pending.device     = pDevice;
        g_pending.bufferSize = bufferSize;
        g_pending.width      = extent.width;
        g_pending.height     = extent.height;
        g_pending.saveBefore = needBefore;
        g_pending.outputDir  = dir;
        g_pending.format     = format;
        g_pending.quality    = quality;

        // Store separate formats/colorspaces for before/after because the effect chain may convert SDR -> HDR
        g_pending.sdrWhitePointNits = sdrWhitePointNits;
        g_pending.vkFormatAfter     = pSwapchain->format;
        g_pending.csmAfter          = getColorSpaceMode(pSwapchain->format, pSwapchain->colorSpace);
        g_pending.vkFormatBefore    = pSwapchain->sourceFormat;
        g_pending.csmBefore         = getColorSpaceMode(pSwapchain->sourceFormat, pSwapchain->sourceColorSpace);
        g_pending.active            = true;

        Logger::debug("Screenshot capture submitted (async).");
    }

    bool processPendingScreenshot()
    {
        if (!g_pending.active) return false;
        LogicalDevice* pDevice = g_pending.device;

        // Check if GPU work is complete (non blocking poll with no timeout)
        VkResult fenceStatus = pDevice->vkd.GetFenceStatus(pDevice->device, g_pending.fence);
        if (fenceStatus != VK_SUCCESS) {
            return false; // GPU still working, try again next frame
        }

        std::vector<ScreenshotWriteTask> tasks;

        // Write after screenshot
        auto readbackBuffer = [&](VkDeviceMemory memory, const std::string& suffix, VkFormat vkFormat,
                                  ColorSpaceMode csm) {
            void* mapped = nullptr;
            if (pDevice->vkd.MapMemory(pDevice->device, memory, 0, g_pending.bufferSize, 0, &mapped) == VK_SUCCESS) {
                ScreenshotWriteTask task;
                task.pixels.resize(g_pending.bufferSize);
                std::memcpy(task.pixels.data(), mapped, g_pending.bufferSize);
                pDevice->vkd.UnmapMemory(pDevice->device, memory);
                task.path              = generateScreenshotPath(g_pending.outputDir, suffix, g_pending.format);
                task.format            = g_pending.format;
                task.quality           = g_pending.quality;
                task.width             = static_cast<int>(g_pending.width);
                task.height            = static_cast<int>(g_pending.height);
                task.vkFormat          = vkFormat;
                task.csm               = csm;
                task.sdrWhitePointNits = g_pending.sdrWhitePointNits;
                tasks.push_back(std::move(task));
            } else {
                Logger::err("Screenshot: Failed to map memory for readback");
            }
        };

        readbackBuffer(g_pending.bufferMemory, "_after", g_pending.vkFormatAfter, g_pending.csmAfter);
        // Write before screenshot
        if (g_pending.saveBefore && g_pending.stagingBufferBefore != VK_NULL_HANDLE) {
            readbackBuffer(g_pending.bufferMemoryBefore, "_before", g_pending.vkFormatBefore, g_pending.csmBefore);
        }

        // Free all GPU resources immediately
        pDevice->vkd.FreeCommandBuffers(pDevice->device, pDevice->commandPool, 1, &g_pending.cmdBuf);
        pDevice->vkd.DestroyFence(pDevice->device, g_pending.fence, nullptr);
        pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBuffer, nullptr);
        pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemory, nullptr);

        if (g_pending.stagingBufferBefore != VK_NULL_HANDLE) {
            pDevice->vkd.DestroyBuffer(pDevice->device, g_pending.stagingBufferBefore, nullptr);
            pDevice->vkd.FreeMemory(pDevice->device, g_pending.bufferMemoryBefore, nullptr);
        }

        // Reset pending state before spawning the thread
        g_pending = PendingScreenshot{};

        if (!tasks.empty()) {
            std::thread(std::move(writeScreenshotsInBackground), std::move(tasks)).detach();
        }
        return true;
    }

} // namespace vkBasalt
