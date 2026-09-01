#include "effect_chain.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <vulkan/vulkan_core.h>

#include "effect.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "overlay_manager.hpp"
#include "imgui_overlay.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "command_buffer.hpp"
#include "format.hpp"
#include "effect_cas.hpp"
#include "effect_clarity.hpp"
#include "effect_clarityrcas.hpp"
#include "effect_crystalclear.hpp"
#include "effect_deband.hpp"
#include "effect_dls.hpp"
#include "effect_fxaa.hpp"
#include "effect_lut.hpp"
#include "effect_reshade.hpp"
#include "effect_smaa.hpp"
#include "effect_transfer.hpp"
#include "pipeline_cache.hpp"
#include "compute_pass.hpp"
#include "compute_test_pass.hpp"
#include "frame_analyzer.hpp"
#include "effect_nit_calibration.hpp"

namespace vkBasalt {

    // Effect Factory
    using EffectCreator = std::function<std::shared_ptr<Effect>(
        LogicalDevice* pLogicalDevice,
        VkFormat unormFormat,
        VkFormat srgbFormat,
        VkExtent2D imageExtent,
        const std::vector<VkImage>& firstImages,
        const std::vector<VkImage>& secondImages,
        Config* pConfig,
        VkColorSpaceKHR colorSpace,
        const std::string& name
    )>;

    static const std::unordered_map<std::string, EffectCreator> builtinEffects = {
        {"fxaa", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<FxaaEffect>(dev, sf, ext, in, out, cfg, cs);
        }},
        {"cas", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<CasEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"deband", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<DebandEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"smaa", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<SmaaEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"lut", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<LutEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"dls", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<DlsEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"clarity", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<ClarityEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"clarityrcas", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<ClarityRcasEffect>(dev, uf, ext, in, out, cfg, cs);
        }},
        {"crystalclear", [](LogicalDevice* dev, VkFormat uf, VkFormat sf, VkExtent2D ext, const std::vector<VkImage>& in, const std::vector<VkImage>& out, Config* cfg, VkColorSpaceKHR cs, const std::string&) {
            return std::make_shared<CrystalClearEffect>(dev, uf, ext, in, out, cfg, cs);
        }}
    };

    static uint32_t getLastDstSlice(uint32_t effectCount) {
        if (effectCount == 0) return 0;
        if (effectCount == 1) return 1;
        uint32_t lastI = effectCount - 1;
        return (lastI % 2 == 1) ? 2 : 1;
    }

    static uint32_t getRequiredSlices(uint32_t effectCount, bool supportsMutable) {
        if (effectCount == 0) return 1; // fallback transfer reads slice 0
        if (effectCount == 1) return supportsMutable ? 1 : 2;
        if (effectCount == 2) return supportsMutable ? 2 : 3;
        return 3; // ping-pong: slices 0, 1, 2
    }

    void buildEffectChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain,
                          VkSwapchainKHR swapchain, Config* pConfig,
                          OverlayManager& overlayManager)
    {
        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {});

        // Append HDR Output Effect (Auto HDR or Nit Calibration)
        std::string autoHdrOpt = pConfig->getOption<std::string>("autoHdr", "on");
        std::string hdrCalibOpt = pConfig->getOption<std::string>("hdrCalibration", "off");
        bool autoHdrEnabled = (autoHdrOpt == "on" || autoHdrOpt == "true" || autoHdrOpt == "1");
        bool hdrCalibEnabled = (hdrCalibOpt == "on" || hdrCalibOpt == "true" || hdrCalibOpt == "1");
        
        ColorSpaceMode srcCsm = getColorSpaceMode(pLogicalSwapchain->sourceFormat, pLogicalSwapchain->sourceColorSpace);
        bool shouldAppendHdrOutput = (srcCsm == ColorSpaceMode::SDR_SRGB && autoHdrEnabled && pLogicalSwapchain->autoHdrActive) ||
                                    (srcCsm != ColorSpaceMode::SDR_SRGB && hdrCalibEnabled);

        // Normal effects operate on the fake images, which are always in the source format.
        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->sourceFormat);
        VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->sourceFormat);

        // Determine which slice compute passes read from. With mutable format the last effect writes to real swapchain images. Otherwise it writes to the last fake slice.
        pLogicalSwapchain->computeSrcSlice =
            pLogicalDevice->supportsMutableFormat ? 0 : getLastDstSlice(effectStrings.size());

        for (uint32_t i = 0; i < effectStrings.size(); i++)
        {
            Logger::debug("current effectString " + effectStrings[i]);

            // Ping-pong slice 0 is the game's render target (read only). Effects alternate between slices 1 and 2 after the first read.
            uint32_t srcSlice, dstSlice;
            if (i == 0) {
                srcSlice = 0;
                dstSlice = 1;
            } else {
                srcSlice = (i % 2 == 1) ? 1 : 2;
                dstSlice = (i % 2 == 1) ? 2 : 1;
            }

            std::vector<VkImage> firstImages(
                pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * srcSlice,
                pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (srcSlice + 1));


            Logger::debug(std::to_string(firstImages.size()) + " images in firstImages (slice " + std::to_string(srcSlice) + ")");
            std::vector<VkImage> secondImages;

            // Last effect writes directly to real swapchain images  (only when no HDR output effect follows)
            if (i == effectStrings.size() - 1 && pLogicalDevice->supportsMutableFormat && !shouldAppendHdrOutput)
            {
                secondImages = pLogicalSwapchain->images;
                Logger::debug("using swapchain images as second images");
            }
            else
            {
                // Intermediate or non-mutable last effect writes to dstSlice
                secondImages = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * dstSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (dstSlice + 1));
                Logger::debug("using fake slice " + std::to_string(dstSlice) + " as second images");
            }
            Logger::debug(std::to_string(secondImages.size()) + " images in secondImages");

            // Dispatch via factory map
            auto it = builtinEffects.find(effectStrings[i]);
            if (it != builtinEffects.end())
            {
                pLogicalSwapchain->effects.push_back(
                    it->second(pLogicalDevice, unormFormat, srgbFormat, pLogicalSwapchain->imageExtent,
                               firstImages, secondImages, pConfig, pLogicalSwapchain->sourceColorSpace, effectStrings[i]));
                Logger::debug("created " + effectStrings[i] + " effect");
            }
            else
            {
                // ReShade fallback
                std::string shaderPath = pConfig->getOption<std::string>("reshadeShaderPath", "");
                if (shaderPath.empty()) shaderPath = pConfig->getOption<std::string>("reshadeTexturePath", "");
                if (shaderPath.empty()) shaderPath = pConfig->getOption<std::string>("reshadeIncludePath", "");

                bool fileExists = false;
                if (!shaderPath.empty()) {
                    if (shaderPath.back() != '/') shaderPath += '/';
                    std::string fullPath = shaderPath + effectStrings[i] + ".fx";
                    std::ifstream f(fullPath.c_str());
                    fileExists = f.good();
                }

                if (fileExists) {
                    pLogicalSwapchain->effects.push_back(std::make_shared<ReshadeEffect>(
                        pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->imageExtent,
                        firstImages, secondImages, pConfig, effectStrings[i]));
                    Logger::debug("created ReshadeEffect for " + effectStrings[i]);
                } else {
                    Logger::err("Unknown or missing effect: '" + effectStrings[i] + "'. Skipping.");
                }
            }
        }

        // Append HDR Output Effect (Auto HDR / Nit Calibration)
        if (shouldAppendHdrOutput) {
            uint32_t lastSlice = getLastDstSlice(effectStrings.size());
            std::vector<VkImage> hdrInputImages;
            if (pLogicalDevice->supportsMutableFormat && effectStrings.empty()) {
                hdrInputImages = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
            } else {
                hdrInputImages = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * lastSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (lastSlice + 1));
            }

            auto hdrEffect = std::make_shared<NitCalibrationEffect>(
                pLogicalDevice, 
                pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat, 
                pLogicalSwapchain->imageExtent,
                hdrInputImages, pLogicalSwapchain->images, 
                pConfig, 
                pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                pLogicalSwapchain->autoHdrActive);
            
            pLogicalSwapchain->effects.push_back(hdrEffect);
            Logger::debug("Appended HDR Output Effect");
        }

        // Non-mutable format add a final transfer from the last fake slice to real swapchain images. Skip when HDR output effect is present it already writes to real swapchain images.
        if (!pLogicalDevice->supportsMutableFormat && !shouldAppendHdrOutput)
        {
            uint32_t transferSrcSlice = getLastDstSlice(effectStrings.size());
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * transferSrcSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (transferSrcSlice + 1)),
                pLogicalSwapchain->images,
                pConfig)));
        }

        // Fallback if no valid effects were created, use plain transfer from slice 0
        if (pLogicalSwapchain->effects.empty())
        {
            Logger::warn("No valid effects could be created; falling back to plain transfer.");
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(),
                                     pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                pLogicalSwapchain->images,
                pConfig)));
        }

        VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
        VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
        VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;

        Logger::debug("effect string count: " + std::to_string(effectStrings.size()));
        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        for (uint32_t ei = 0; ei < pLogicalSwapchain->effects.size(); ei++)
        {
            pLogicalSwapchain->effects[ei]->useDepthImage(depthImageView);
            pLogicalSwapchain->effects[ei]->setChainPosition(
                ei == 0,
                ei == pLogicalSwapchain->effects.size() - 1);
        }

        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        if (swapchain != VK_NULL_HANDLE) {
            Logger::debug("allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain " + convertToString(swapchain));
        } else {
            Logger::debug("allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain (rebuild)");
        }

        // Create compute passes. They read from the final output slice.
        if (pLogicalSwapchain->computePasses.empty())
        {
            std::vector<VkImage> computeSrcImages;
            if (pLogicalDevice->supportsMutableFormat)
            {
                computeSrcImages = pLogicalSwapchain->images;
            }
            else
            {
                uint32_t srcSlice = pLogicalSwapchain->computeSrcSlice;
                computeSrcImages = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * srcSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (srcSlice + 1));
            }

            pLogicalSwapchain->computePasses.push_back(std::make_shared<FrameAnalyzer>(
                pLogicalDevice, pLogicalSwapchain->imageExtent, computeSrcImages,
                pLogicalSwapchain->format, pLogicalSwapchain->colorSpace));
            Logger::debug("created compute passes (FrameAnalyzer)");
        }

        writeCommandBuffers(pLogicalDevice, pLogicalSwapchain, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);
        Logger::debug("wrote CommandBuffers");

        // Only create semaphores on initial setup. Rebuilds preserve them as imageCount is fixed for a given swapchain.
        if (pLogicalSwapchain->semaphores.empty()) {
            pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
            Logger::debug("created semaphores");
        }

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersEffect[i]));
        }

        // Default chain for when effects are toggled off, preserved across rebuilds. When Auto HDR is active, we must still convert SDR->HDR even with effects off, lest the display receives SDR values in an HDR colorspace (teal/cyan artifact).
        if (!pLogicalSwapchain->defaultTransfer) {
            std::vector<std::shared_ptr<Effect>> noEffectChain;

            if (pLogicalSwapchain->autoHdrActive) {
                // Use NitCalibrationEffect for SDR->HDR conversion (slice 0 -> real swapchain)
                std::vector<VkImage> slice0Images(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
                pLogicalSwapchain->defaultHdrEffect = std::make_shared<NitCalibrationEffect>(
                    pLogicalDevice,
                    pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat,
                    pLogicalSwapchain->imageExtent,
                    slice0Images, pLogicalSwapchain->images,
                    pConfig,
                    pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                    pLogicalSwapchain->autoHdrActive);
                pLogicalSwapchain->defaultHdrEffect->setChainPosition(true, true);
                noEffectChain.push_back(pLogicalSwapchain->defaultHdrEffect);
            } else {
                // No Auto HDR: simple transfer
                pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
                    pLogicalDevice,
                    pLogicalSwapchain->format,
                    pLogicalSwapchain->imageExtent,
                    std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(),
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                    pLogicalSwapchain->images,
                    pConfig));
                pLogicalSwapchain->defaultTransfer->setChainPosition(true, true);
                noEffectChain.push_back(pLogicalSwapchain->defaultTransfer);
            }

            pLogicalSwapchain->commandBuffersNoEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
            writeCommandBuffers(pLogicalDevice,
                                pLogicalSwapchain,
                                noEffectChain,
                                VK_NULL_HANDLE,
                                VK_NULL_HANDLE,
                                VK_FORMAT_UNDEFINED,
                                pLogicalSwapchain->commandBuffersNoEffect);
        }

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written noEffect commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersNoEffect[i]));
        }

        // Initialize ImGui Overlay eagerly while the queue is idle, the overlay renders on top of the real swapchain images, so it must use destFormat (HDR), not sourceFormat (SDR).
        if (swapchain != VK_NULL_HANDLE) {
            VkFormat overlayFormat = convertToUNORM(pLogicalSwapchain->destFormat);
            overlayManager.initOverlay(pLogicalDevice, pLogicalSwapchain, swapchain, overlayFormat, pConfig);
        }

        // Save pipeline cache only on initial setup so that we don't write to disk for every slider adjustment. The cache is already saved in vkBasalt_DestroyDevice for normal exits.
        if (swapchain != VK_NULL_HANDLE) {
            savePipelineCacheData(pLogicalDevice->device, pLogicalDevice->vkd,
                pLogicalDevice->pipelineCache, pLogicalDevice->pipelineCachePath);
        }
    }

    void rebuildEffectChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain,
                            Config* pConfig, OverlayManager& overlayManager,
                            bool waitForIdle)
    {
        Logger::debug("Rebuilding effects for swapchain...");

        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {});
        if (waitForIdle) {
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
        }
        
        ColorSpaceMode srcCsm = getColorSpaceMode(pLogicalSwapchain->sourceFormat, pLogicalSwapchain->sourceColorSpace);
        uint32_t totalEffectCount = (uint32_t)effectStrings.size();
        
        bool reserveHdrOutput = false;
        if (srcCsm == ColorSpaceMode::SDR_SRGB) {
            std::string autoHdr = pConfig->getOption<std::string>("autoHdr", "on");
            if ((autoHdr == "on" || autoHdr == "true" || autoHdr == "1") && pLogicalSwapchain->autoHdrActive) {
                reserveHdrOutput = true;
            }
        } else {
            std::string hdrMode = pConfig->getOption<std::string>("hdrCalibration", "off");
            if (hdrMode == "on" || hdrMode == "true" || hdrMode == "1") {
                reserveHdrOutput = true;
            }
        }
        
        if (reserveHdrOutput) {
            totalEffectCount += 1;
        }
        
        uint32_t requiredSlices = getRequiredSlices(totalEffectCount, pLogicalDevice->supportsMutableFormat);
        uint32_t requiredFakeImageCount = pLogicalSwapchain->imageCount * requiredSlices;

        // Chain GREW beyond allocated pool, game holds old VkImage handles, so we must force the game to recreate its swapchain.
        if (requiredFakeImageCount > pLogicalSwapchain->fakeImages.size()) {
            Logger::debug("Effect chain grew beyond allocated pool. Forcing swapchain rebuild...");
            pLogicalSwapchain->forceSwapchainRebuild = true;

            if (!pLogicalSwapchain->commandBuffersEffect.empty()) {
                pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool,
                                                       pLogicalSwapchain->commandBuffersEffect.size(),
                                                       pLogicalSwapchain->commandBuffersEffect.data());
                pLogicalSwapchain->commandBuffersEffect.clear();
            }
            if (!pLogicalSwapchain->commandBuffersNoEffect.empty()) {
                pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool,
                                                       pLogicalSwapchain->commandBuffersNoEffect.size(),
                                                       pLogicalSwapchain->commandBuffersNoEffect.data());
                pLogicalSwapchain->commandBuffersNoEffect.clear();
            }

            pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
            pLogicalSwapchain->commandBuffersNoEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);

            pLogicalSwapchain->effects.clear();
            pLogicalSwapchain->defaultTransfer.reset();
            pLogicalSwapchain->defaultHdrEffect.reset();

            std::vector<std::shared_ptr<Effect>> noEffectChain;
            if (pLogicalSwapchain->autoHdrActive) {
                std::vector<VkImage> slice0Images(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
                pLogicalSwapchain->defaultHdrEffect = std::make_shared<NitCalibrationEffect>(
                    pLogicalDevice,
                    pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat,
                    pLogicalSwapchain->imageExtent,
                    slice0Images, pLogicalSwapchain->images,
                    pConfig,
                    pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                    pLogicalSwapchain->autoHdrActive);
                pLogicalSwapchain->defaultHdrEffect->setChainPosition(true, true);
                noEffectChain.push_back(pLogicalSwapchain->defaultHdrEffect);
            } else {
                pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
                    pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                    std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(),
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                    pLogicalSwapchain->images, pConfig));
                pLogicalSwapchain->defaultTransfer->setChainPosition(true, true);
                noEffectChain.push_back(pLogicalSwapchain->defaultTransfer);
            }
            writeCommandBuffers(pLogicalDevice, pLogicalSwapchain, noEffectChain,
                                VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED,
                                pLogicalSwapchain->commandBuffersNoEffect);
            return; // Wait for the game to handle VK_ERROR_OUT_OF_DATE_KHR
        }

        // Chain SHRUNK or same size, rebuild in-place using existing pool. The game's cached VkImage handles remain valid.
        Logger::debug("Effect chain fits in existing pool. Rebuilding in-place...");

        if (!pLogicalSwapchain->commandBuffersEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool,
                                                   pLogicalSwapchain->commandBuffersEffect.size(),
                                                   pLogicalSwapchain->commandBuffersEffect.data());
            pLogicalSwapchain->commandBuffersEffect.clear();
        }

        pLogicalSwapchain->effects.clear();
        buildEffectChain(pLogicalDevice, pLogicalSwapchain, VK_NULL_HANDLE, pConfig, overlayManager);
        Logger::debug("Rebuild complete.");
    }
} // namespace vkBasalt
