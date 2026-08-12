#include "effect_chain.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "fake_swapchain.hpp"
#include "config.hpp"
#include "overlay_manager.hpp"
#include "imgui_overlay.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "image_view.hpp"
#include "memory.hpp"
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

#include <fstream>
#include <unistd.h>
#include <algorithm>

namespace vkBasalt {

    void buildEffectChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain,
                          VkSwapchainKHR swapchain, Config* pConfig,
                          OverlayManager& overlayManager)
    {
        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});

        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->format);

        // Per effect image slicing. Each effect reads from one slice of fake images and writes to the next.
        // The last effect writes to the real swapchain images.
        for (uint32_t i = 0; i < effectStrings.size(); i++)
        {
            Logger::debug("current effectString " + effectStrings[i]);

            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * i,
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1));
            Logger::debug(std::to_string(firstImages.size()) + " images in firstImages");

            std::vector<VkImage> secondImages;
            if (i == effectStrings.size() - 1)
            {
                // Last effect output goes to real swapchain images (mutable) or the final fake slice (non-mutable)
                secondImages = pLogicalDevice->supportsMutableFormat
                                 ? pLogicalSwapchain->images
                                 : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount,
                                                         pLogicalSwapchain->fakeImages.end());
                Logger::debug("using swapchain images as second images");
            }
            else
            {
                // Intermediate effect output goes to the next fake image slice
                secondImages = std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1),
                                                     pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 2));
                Logger::debug("not using swapchain images as second images");
            }
            Logger::debug(std::to_string(secondImages.size()) + " images in secondImages");

            if (effectStrings[i] == "fxaa")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new FxaaEffect(pLogicalDevice, srgbFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created FxaaEffect");
            }
            else if (effectStrings[i] == "cas")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new CasEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created CasEffect");
            }
            else if (effectStrings[i] == "deband")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DebandEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created DebandEffect");
            }
            else if (effectStrings[i] == "smaa")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new SmaaEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created SmaaEffect");
            }
            else if (effectStrings[i] == "lut")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new LutEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created LutEffect");
            }
            else if (effectStrings[i] == "dls")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DlsEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                Logger::debug("created DlsEffect");
            }
            else if (effectStrings[i] == "clarity")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new ClarityEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig, pLogicalSwapchain->colorSpace)));
                Logger::debug("created ClarityEffect");
            }
            else if (effectStrings[i] == "clarityrcas")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new ClarityRcasEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig, pLogicalSwapchain->colorSpace)));
                Logger::debug("created ClarityRcasEffect");
            }
            else if (effectStrings[i] == "crystalclear")
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new CrystalClearEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig, pLogicalSwapchain->colorSpace)));
                Logger::debug("created CrystalClearEffect");
            }
            else
            {
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
                    pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new ReshadeEffect(pLogicalDevice,
                                                                                                    pLogicalSwapchain->format,
                                                                                                    pLogicalSwapchain->imageExtent,
                                                                                                    firstImages,
                                                                                                    secondImages,
                                                                                                    pConfig,
                                                                                                    effectStrings[i])));
                    Logger::debug("created ReshadeEffect for " + effectStrings[i]);
                } else {
                    Logger::err("Unknown or missing effect: '" + effectStrings[i] + "'. Skipping.");
                }
            }
        }

        // Non-mutable format add a final transfer from the last fake slice to real swapchain images
        if (!pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images,
                pConfig)));
        }

        // Fallback if no valid effects were created, use plain transfer
        if (pLogicalSwapchain->effects.empty())
        {
            Logger::warn("No valid effects could be created; falling back to plain transfer.");
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                pLogicalSwapchain->images,
                pConfig)));
        }

        VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
        VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
        VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;

        Logger::debug("effect string count: " + std::to_string(effectStrings.size()));
        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        for (auto& effect : pLogicalSwapchain->effects)
        {
            effect->useDepthImage(depthImageView);
        }

        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        if (swapchain != VK_NULL_HANDLE) {
            Logger::debug("allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain " + convertToString(swapchain));
        } else {
            Logger::debug("allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain (rebuild)");
        }

        writeCommandBuffers(pLogicalDevice, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);
        Logger::debug("wrote CommandBuffers");

        pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        Logger::debug("created semaphores");

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersEffect[i]));
        }

        // Default transfer for when effects are toggled off
        pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
            pLogicalDevice,
            pLogicalSwapchain->format,
            pLogicalSwapchain->imageExtent,
            std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
            pLogicalSwapchain->images,
            pConfig));

        pLogicalSwapchain->commandBuffersNoEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);

        writeCommandBuffers(pLogicalDevice,
                            {pLogicalSwapchain->defaultTransfer},
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            VK_FORMAT_UNDEFINED,
                            pLogicalSwapchain->commandBuffersNoEffect);

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written noEffect commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersNoEffect[i]));
        }

        // Initialize ImGui Overlay eagerly while the queue is idle
        if (swapchain != VK_NULL_HANDLE) {
            overlayManager.initOverlay(pLogicalDevice, pLogicalSwapchain, swapchain, unormFormat, pConfig);
        }

        // Save pipeline cache after all pipelines are compiled. Game might exit without calling vkDestroyDevice, so this is the reliable save point.
        savePipelineCacheData(pLogicalDevice->device, pLogicalDevice->vkd,
                              pLogicalDevice->pipelineCache, pLogicalDevice->pipelineCachePath);
    }

    void rebuildEffectChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain,
                            Config* pConfig, OverlayManager& overlayManager,
                            bool waitForIdle)
    {
        Logger::debug("Rebuilding effects for swapchain...");

        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});

        if (waitForIdle) {
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
        }

        uint32_t requiredFakeImageCount = pLogicalSwapchain->imageCount * (effectStrings.size() + !pLogicalDevice->supportsMutableFormat);

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

            pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                pLogicalSwapchain->images, pConfig));

            writeCommandBuffers(pLogicalDevice, {pLogicalSwapchain->defaultTransfer}, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, pLogicalSwapchain->commandBuffersNoEffect);
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
        if (!pLogicalSwapchain->commandBuffersNoEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool,
                                                   pLogicalSwapchain->commandBuffersNoEffect.size(),
                                                   pLogicalSwapchain->commandBuffersNoEffect.data());
            pLogicalSwapchain->commandBuffersNoEffect.clear();
        }
        for (auto sem : pLogicalSwapchain->semaphores) {
            pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, sem, nullptr);
        }
        pLogicalSwapchain->semaphores.clear();
        pLogicalSwapchain->effects.clear();
        pLogicalSwapchain->defaultTransfer.reset();

        buildEffectChain(pLogicalDevice, pLogicalSwapchain, VK_NULL_HANDLE, pConfig, overlayManager);

        if (waitForIdle) {
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
        }

        Logger::debug("Rebuild complete.");
    }

} // namespace vkBasalt
