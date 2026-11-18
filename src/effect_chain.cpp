#include "effect_chain.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
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
#include "effect_cmaa2.hpp"
#include "effect_transfer.hpp"
#include "pipeline_cache.hpp"
#include "compute_pass.hpp"
#include "frame_analyzer.hpp"
#include "effect_nit_calibration.hpp"
#include "effect_hdr_debug.hpp"
#include "layer_globals.hpp"

namespace vkBasalt
{
    bool isHdrOutputNeeded(Config* pConfig, LogicalSwapchain* pLogicalSwapchain)
    {
        ColorSpaceMode srcCsm = getColorSpaceMode(pLogicalSwapchain->sourceFormat, pLogicalSwapchain->sourceColorSpace);
        if (srcCsm == ColorSpaceMode::SDR_SRGB) {
            return pLogicalSwapchain->autoHdrActive;
        } else {
            // Native HDR: "off" - no processing, "passthrough" and "manual" both require the effect in the chain.
            auto mode = pConfig->getOption<std::string>("hdrCalibrationMode", "passthrough");
            return mode != "off";
        }
    }

    // The config parser turns "effects =" into [""], and an empty name otherwise reads as a real effect entry. Left
    // unfiltered, every consumer that sizes the fake image pool or assigns slices counts a phantom effect.
    // The pool grows to 2 slices for what is actually a passthrough chain. All call sites share this prune now.
    void pruneEmptyEffectNames(std::vector<std::string>& effectStrings)
    {
        effectStrings.erase(
            std::remove_if(effectStrings.begin(), effectStrings.end(), [](const std::string& s) { return s.empty(); }),
            effectStrings.end());
    }

    // Backstop cap for retired compute passes. QueueWaitIdle makes eviction GPU safe. The cap bounds how many dead FrameAnalyzers
    // we keep alive for CPU side ImGui scope descriptor lifetime. Derive it from swapchain depth so it scales with FIFO-2 vs mailbox-3+ setups.
    // The floor of 4 keeps rapid toggle sequences safe when imageCount is unusually small.
    static void trimComputePassGraveyard(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain)
    {
        const uint32_t graveyardCap = std::max(4u, pLogicalSwapchain->imageCount + 2);
        if (pLogicalSwapchain->computePassGraveyard.size() <= graveyardCap) return;

        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        const size_t excess = pLogicalSwapchain->computePassGraveyard.size() - graveyardCap;
        pLogicalSwapchain->computePassGraveyard.erase(
            pLogicalSwapchain->computePassGraveyard.begin(), pLogicalSwapchain->computePassGraveyard.begin() + excess);
    }

    static void
    buildDefaultNoEffectChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, Config* pConfig)
    {
        std::vector<std::shared_ptr<Effect>> noEffectChain;

        // Free old no effect command buffers before allocating new ones to prevent leaks
        if (!pLogicalSwapchain->commandBuffersNoEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersNoEffect.size(),
                pLogicalSwapchain->commandBuffersNoEffect.data());
            pLogicalSwapchain->commandBuffersNoEffect.clear();
        }

        if (pLogicalSwapchain->fakeImages.size() < pLogicalSwapchain->imageCount) {
            Logger::err(
                "buildDefaultNoEffectChain: fakeImages pool too small ("
                + std::to_string(pLogicalSwapchain->fakeImages.size()) + " < "
                + std::to_string(pLogicalSwapchain->imageCount) + "). Skipping.");
            return;
        }

        if (isHdrOutputNeeded(pConfig, pLogicalSwapchain)) {
            // Create dedicated HDR effects for the bypassed chain that read from slice 0. The main chain's NitCalibrationEffect reads from
            // the last processed slice, but in bypass mode the game renders directly to slice 0 with no intermediate effects.
            std::vector<VkImage> slice0Images(
                pLogicalSwapchain->fakeImages.begin(),
                pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);

            // Inject HDR Debug Pattern if active. Store persistently so the UBO survives
            // beyond this function scope (command buffers reference its descriptor sets).
            if (g_hdrDebugToolActive.load()) {
                pLogicalSwapchain->defaultHdrDebugEffect = std::make_shared<HdrDebugEffect>(
                    pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->imageExtent, slice0Images,
                    pConfig);
                noEffectChain.push_back(pLogicalSwapchain->defaultHdrDebugEffect);
            }

            pLogicalSwapchain->defaultHdrEffect = std::make_shared<NitCalibrationEffect>(
                pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat,
                pLogicalSwapchain->imageExtent, slice0Images, pLogicalSwapchain->images, pConfig,
                pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                pLogicalSwapchain->autoHdrActive, pLogicalSwapchain->monitorName);
            noEffectChain.push_back(pLogicalSwapchain->defaultHdrEffect);
            Logger::debug("buildDefaultNoEffectChain: HDR output effect included in no-effect chain");
        } else {
            pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                pLogicalSwapchain->images, pConfig));
            pLogicalSwapchain->defaultTransfer->setChainPosition(true, true);
            noEffectChain.push_back(pLogicalSwapchain->defaultTransfer);
        }

        pLogicalSwapchain->commandBuffersNoEffect =
            allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        writeCommandBuffers(
            pLogicalDevice, pLogicalSwapchain, noEffectChain, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED,
            pLogicalSwapchain->commandBuffersNoEffect);
    }

    void rebuildFallbackChain(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, Config* pConfig)
    {
        // This path keeps the existing image pool. Sizing decisions are made by rebuildEffectChain before it's called.
        // Wait for GPU to finish executing the previous frame's command buffers before freeing them. Freeing in flight command buffers
        // is undefined behavior, causes crashes when AutoHDR is toggled or the last effect is removed.
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        if (!pLogicalSwapchain->commandBuffersEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersEffect.size(),
                pLogicalSwapchain->commandBuffersEffect.data());
            pLogicalSwapchain->commandBuffersEffect.clear();
        }
        if (!pLogicalSwapchain->commandBuffersNoEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersNoEffect.size(),
                pLogicalSwapchain->commandBuffersNoEffect.data());
            pLogicalSwapchain->commandBuffersNoEffect.clear();
        }

        // Move compute passes to the graveyard instead of destroying them immediately. The overlay may still hold scope texture descriptor
        // sets referencing images owned by the FrameAnalyzer. Destroying them now would cause use after free.
        for (auto& pass : pLogicalSwapchain->computePasses) {
            pLogicalSwapchain->computePassGraveyard.push_back(pass);
        }
        pLogicalSwapchain->computePasses.clear();

        trimComputePassGraveyard(pLogicalDevice, pLogicalSwapchain);

        pLogicalSwapchain->effects.clear();
        pLogicalSwapchain->nitCalibrationEffect = nullptr;
        pLogicalSwapchain->defaultTransfer.reset();
        pLogicalSwapchain->defaultHdrEffect.reset();
        pLogicalSwapchain->defaultHdrDebugEffect.reset();
        buildDefaultNoEffectChain(pLogicalDevice, pLogicalSwapchain, pConfig);
    }

    // Effect Factory metadata
    struct EffectFactoryInfo
    {
        using Creator = std::function<std::shared_ptr<Effect>(
            LogicalDevice* pLogicalDevice,
            VkFormat unormFormat,
            VkFormat srgbFormat,
            VkExtent2D imageExtent,
            const std::vector<VkImage>& firstImages,
            const std::vector<VkImage>& secondImages,
            Config* pConfig,
            VkColorSpaceKHR colorSpace,
            const std::string& name)>;

        Creator creator;
        bool requiresStorageBit; // effect writes into an image that must carry VK_IMAGE_USAGE_STORAGE_BIT
        bool supportsInPlace;    // effect may read and write the same image buffer (needs no second fake slice)
    };

    static const std::unordered_map<std::string, EffectFactoryInfo> builtinEffects = {
        {"fxaa",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<FxaaEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"cas",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<CasEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"deband",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<DebandEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"smaa",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<SmaaEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"lut",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<LutEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"dls",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<DlsEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"clarity",
         {.creator = [](LogicalDevice* dev,
                        VkFormat uf,
                        VkFormat /*sf*/,
                        VkExtent2D ext,
                        const std::vector<VkImage>& in,
                        const std::vector<VkImage>& out,
                        Config* cfg,
                        VkColorSpaceKHR cs,
                        const std::string&) { return std::make_shared<ClarityEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"clarityrcas",
         {.creator =
              [](LogicalDevice* dev,
                 VkFormat uf,
                 VkFormat /*sf*/,
                 VkExtent2D ext,
                 const std::vector<VkImage>& in,
                 const std::vector<VkImage>& out,
                 Config* cfg,
                 VkColorSpaceKHR cs,
                 const std::string&) { return std::make_shared<ClarityRcasEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"crystalclear",
         {.creator =
              [](LogicalDevice* dev,
                 VkFormat uf,
                 VkFormat /*sf*/,
                 VkExtent2D ext,
                 const std::vector<VkImage>& in,
                 const std::vector<VkImage>& out,
                 Config* cfg,
                 VkColorSpaceKHR cs,
                 const std::string&) { return std::make_shared<CrystalClearEffect>(dev, uf, ext, in, out, cfg, cs); },
          .requiresStorageBit = false}},
        {"cmaa2",
         {
             .creator =
                 [](LogicalDevice* dev,
                    VkFormat uf,
                    VkFormat sf,
                    VkExtent2D ext,
                    const std::vector<VkImage>& in,
                    const std::vector<VkImage>& out,
                    Config* cfg,
                    VkColorSpaceKHR cs,
                    const std::string&) { return std::make_shared<Cmaa2Effect>(dev, uf, ext, in, out, cfg, cs); },
             .requiresStorageBit = true, // needs STORAGE_BIT on the image it writes
             .supportsInPlace    = true  // its deferred apply pass writes back into the image it sampled
         }},
    };

    bool effectRequiresStorageBit(const std::string& name)
    {
        auto it = builtinEffects.find(name);
        return (it != builtinEffects.end()) ? it->second.requiresStorageBit : false;
    }

    bool effectSupportsInPlace(const std::string& name)
    {
        auto it = builtinEffects.find(name);
        return (it != builtinEffects.end()) ? it->second.supportsInPlace : false;
    }

    bool scopesActivelyViewed()
    {
        // 60 frames of hysteresis: bridges the 1-2 frames where renderOverlay is skipped during an
        // OUT_OF_DATE rebuild, and tolerates games that take a few frames to recreate the swapchain.
        constexpr uint64_t ACTIVE_WINDOW = 60;
        uint64_t current                 = g_globalFrameCounter.load(std::memory_order_relaxed);
        uint64_t lastViewed              = g_scopesLastViewedFrame.load(std::memory_order_relaxed);
        return lastViewed != 0 && (current - lastViewed) < ACTIVE_WINDOW;
    }

    // Whether the last normal effect may render straight into the real swapchain images, skipping the final transfer.
    // Render pass effects only need COLOR_ATTACHMENT_BIT, which every swapchain format supports.
    bool
    canWriteDirectToRealSwapchain(LogicalDevice* pLogicalDevice, VkFormat realSwapchainFormat, const std::string& name)
    {
        if (effectRequiresStorageBit(name)) return false;

        // The effect renders through its UNORM output view. CreateSwapchainKHR only adds that view format when the device supports mutable format.
        // So without it an SRGB real swapchain can't expose the view and the write is invalid. Fall back to a fake slice plus the trailing transfer.
        if (!pLogicalDevice || !pLogicalDevice->supportsMutableFormat) return false;

        (void) realSwapchainFormat;
        return true;
    }

    // Ping pong buffering: fake slices 0 and 1 strictly alternate, slice 0 being the game's initial render target.
    // One slice suffices when nothing has to be ping-ponged into a second buffer:
    // - no normal effects at all (chain is a transfer, or HDR output reads slice 0 and writes the real images)
    // - one normal effect that can direct write the real images (graphics effect with no HDR output appended)
    // - one normal effect that works in place (reads and writes slice 0; transfer/HDR then reads slice 0)
    uint32_t getRequiredFakeSlices(
        LogicalDevice* pLogicalDevice,
        VkFormat realSwapchainFormat,
        const std::vector<std::string>& effectStrings,
        bool hdrOutputNeeded,
        bool effectsBypassed)
    {
        if (effectsBypassed || effectStrings.empty()) return 1;

        if (effectStrings.size() == 1) {
            const std::string& name = effectStrings[0];
            if (!hdrOutputNeeded && canWriteDirectToRealSwapchain(pLogicalDevice, realSwapchainFormat, name)) return 1;
            if (effectSupportsInPlace(name)) return 1;
            return 2;
        }
        return 2;
    }

    void buildEffectChain(
        LogicalDevice* pLogicalDevice,
        LogicalSwapchain* pLogicalSwapchain,
        VkSwapchainKHR swapchain,
        Config* pConfig,
        OverlayManager& overlayManager)
    {
        // Wait for GPU to finish executing previous frames before resetting/freeing command buffers.
        // This prevents VUID-vkResetCommandBuffer-commandBuffer-00045 when the game recreates the swapchain without calling vkDeviceWaitIdle
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        auto effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {});
        pruneEmptyEffectNames(effectStrings);

        // Append HDR Output Effect (Auto HDR or Nit Calibration)
        auto autoHdrOpt       = pConfig->getOption<std::string>("autoHdr", "on");
        bool autoHdrEnabled   = (autoHdrOpt == "on" || autoHdrOpt == "true" || autoHdrOpt == "1");
        ColorSpaceMode srcCsm = getColorSpaceMode(pLogicalSwapchain->sourceFormat, pLogicalSwapchain->sourceColorSpace);
        bool forceHdrPgo      = (std::getenv("VKBASALT_FORCE_HDR_PGO") != nullptr);
        bool shouldAppendHdrOutput =
            (srcCsm == ColorSpaceMode::SDR_SRGB && (autoHdrEnabled || forceHdrPgo) && pLogicalSwapchain->autoHdrActive)
            || (srcCsm != ColorSpaceMode::SDR_SRGB
                && pConfig->getOption<std::string>("hdrCalibrationMode", "passthrough") != "off");

        // Normal effects operate on the fake images, which are always in the source format.
        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->sourceFormat);
        VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->sourceFormat);

        // If the game outputs scRGB (16F) or HDR10 (10-bit), convertToUNORM might mistakenly return an 8-bit UNORM format, causing GPU to clamp all >1.0 linear
        // values to 1.0 during the intermediate effect's render pass, crushing bright saturated colors (like orange) into desaturated higlights before NitCalibration can tonemap.
        if (isExtendedRangeFormat(pLogicalSwapchain->sourceFormat)) {
            unormFormat = pLogicalSwapchain->sourceFormat;
            srgbFormat  = pLogicalSwapchain->sourceFormat;
        }

        // computeSrcSlice is set after the effect loop using pLogicalSwapchain->lastEffectWroteToReal and finalFakeSlice.
        // We initialize it to 0 here; it will be updated below after the loop completes.
        pLogicalSwapchain->computeSrcSlice = 0;

        // These two are written inside the effect loop below, which is skipped entirely when the chain is bypassed.
        // The per effect body never runs for an empty chain. LogicalSwapchain outlives an in place rebuild. And without this reset,
        // they carry the previous chain's values into the new one. Downstream, hdrInputImages, transferSrcSlice and computeSrcSlice all index
        // fakeImages as begin() + imageCount * slice. A stale finalFakeSlice of 1 against a pool that shrunk to one slice reads past the end of the vector.
        pLogicalSwapchain->lastEffectWroteToReal = false;
        pLogicalSwapchain->finalFakeSlice        = 0;

        bool bypassedAndReclaimed = !g_effectsEnabled.load() && g_vramReclaimedForCurrentBypass;

        if (!bypassedAndReclaimed) {
            uint32_t currentFakeSlice = 0;
            for (uint32_t i = 0; i < effectStrings.size(); i++) {
                Logger::debug("current effectString " + effectStrings[i]);
                bool isLast    = (i == effectStrings.size() - 1);
                bool isInPlace = effectSupportsInPlace(effectStrings[i]);

                // Direct write is only safe for the last effect. A mid-chain write would clobber the presentable image before the chain finished.
                // The next effect would have to sample a real image in destFormat while the chain works in sourceFormat. It's also skipped when
                // an HDR output effect follows, since that effect reads a fake slice.
                // canWriteDirectToRealSwapchain documents why compute effects are excluded even on storage bit capable formats.
                bool canDirectWrite = false;
                if (isLast && !shouldAppendHdrOutput) {
                    canDirectWrite =
                        canWriteDirectToRealSwapchain(pLogicalDevice, pLogicalSwapchain->format, effectStrings[i]);
                }

                if (canDirectWrite) isInPlace = false; // Direct write means input (fake) != output (real)

                uint32_t srcSlice = currentFakeSlice;
                uint32_t dstSlice = isInPlace ? srcSlice : (1 - srcSlice); // Ping-pong

                std::vector<VkImage> firstImages(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * srcSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (srcSlice + 1));
                Logger::debug(
                    std::to_string(firstImages.size()) + " images in firstImages (slice " + std::to_string(srcSlice)
                    + ")");

                std::vector<VkImage> secondImages;
                if (canDirectWrite) {
                    secondImages = pLogicalSwapchain->images;
                    Logger::debug("using swapchain images as second images");
                } else {
                    secondImages = std::vector<VkImage>(
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * dstSlice,
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (dstSlice + 1));
                    Logger::debug("using fake slice " + std::to_string(dstSlice) + " as second images");
                }
                Logger::debug(std::to_string(secondImages.size()) + " images in secondImages");

                // Dispatch via factory map
                auto it = builtinEffects.find(effectStrings[i]);
                if (it != builtinEffects.end()) {
                    auto effect = it->second.creator(
                        pLogicalDevice, unormFormat, srgbFormat, pLogicalSwapchain->imageExtent, firstImages,
                        secondImages, pConfig, pLogicalSwapchain->sourceColorSpace, effectStrings[i]);
                    effect->setInPlace(isInPlace);
                    pLogicalSwapchain->effects.push_back(effect);
                    Logger::debug("created " + effectStrings[i] + " effect" + (isInPlace ? " (in-place)" : ""));
                } else {
                    // ReShade fallback
                    auto shaderPath = pConfig->getOption<std::string>("reshadeShaderPath", "");
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
                        pLogicalSwapchain->effects.push_back(
                            std::make_shared<ReshadeEffect>(
                                pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->imageExtent,
                                firstImages, secondImages, pConfig, effectStrings[i]));
                        Logger::debug("created ReshadeEffect for " + effectStrings[i]);
                    } else {
                        Logger::err("Unknown or missing effect: '" + effectStrings[i] + "'. Skipping.");
                    }
                }

                // Advance slice for the next effect, unless the current effect was in-place
                if (!isInPlace) {
                    currentFakeSlice = dstSlice;
                }

                // Save state for the final effect
                if (isLast) {
                    pLogicalSwapchain->lastEffectWroteToReal = canDirectWrite;
                }
            }
            // Save the final fake slice for HDR input and TransferEffect fallback
            pLogicalSwapchain->finalFakeSlice = currentFakeSlice;
        }

        // Append HDR Output Effect (Auto HDR / Nit Calibration)
        pLogicalSwapchain->nitCalibrationEffect = nullptr;
        if (shouldAppendHdrOutput && !bypassedAndReclaimed) {
            std::vector<VkImage> hdrInputImages;
            if (effectStrings.empty()) {
                hdrInputImages = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
            } else {
                // Use the state tracked during the effect loop
                uint32_t lastSlice = pLogicalSwapchain->finalFakeSlice;
                hdrInputImages     = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * lastSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (lastSlice + 1));
            }

            // Inject HDR Metadata Debug Pattern Effect if active. It writes the test pattern to hdrInputImages, which NitCalibrationEffect will then read and tone map.
            if (g_hdrDebugToolActive.load()) {
                auto debugEffect = std::make_shared<HdrDebugEffect>(
                    pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->imageExtent, hdrInputImages,
                    pConfig);
                pLogicalSwapchain->effects.push_back(debugEffect);
                Logger::debug("Injected HDR Metadata Debug Pattern Effect");
            }

            auto hdrEffect = std::make_shared<NitCalibrationEffect>(
                pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat,
                pLogicalSwapchain->imageExtent, hdrInputImages, pLogicalSwapchain->images, pConfig,
                pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                pLogicalSwapchain->autoHdrActive, pLogicalSwapchain->monitorName);

            pLogicalSwapchain->effects.push_back(hdrEffect);
            pLogicalSwapchain->nitCalibrationEffect = hdrEffect.get();
            Logger::debug("Appended HDR Output Effect");
        }

        // Add a final transfer from the last fake slice to real swapchain images when the last effect didn't write directly to real images.
        if (!pLogicalSwapchain->lastEffectWroteToReal && !shouldAppendHdrOutput) {
            uint32_t transferSrcSlice =
                (bypassedAndReclaimed || effectStrings.empty()) ? 0 : pLogicalSwapchain->finalFakeSlice;
            pLogicalSwapchain->effects.push_back(
                std::shared_ptr<Effect>(new TransferEffect(
                    pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                    std::vector<VkImage>(
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * transferSrcSlice,
                        pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (transferSrcSlice + 1)),
                    pLogicalSwapchain->images, pConfig)));
        }

        // Fallback if no valid effects were created
        if (pLogicalSwapchain->effects.empty()) {
            if (shouldAppendHdrOutput) {
                std::vector<VkImage> slice0Images(
                    pLogicalSwapchain->fakeImages.begin(),
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
                pLogicalSwapchain->effects.push_back(
                    std::make_shared<NitCalibrationEffect>(
                        pLogicalDevice, pLogicalSwapchain->sourceFormat, pLogicalSwapchain->destFormat,
                        pLogicalSwapchain->imageExtent, slice0Images, pLogicalSwapchain->images, pConfig,
                        pLogicalSwapchain->sourceColorSpace, pLogicalSwapchain->destColorSpace,
                        pLogicalSwapchain->autoHdrActive, pLogicalSwapchain->monitorName));
                pLogicalSwapchain->nitCalibrationEffect = pLogicalSwapchain->effects.back().get();
                Logger::warn("Fallback: using NitCalibrationEffect for SDR->HDR conversion.");
            } else {
                Logger::warn("No valid effects could be created; falling back to plain transfer.");
                pLogicalSwapchain->effects.push_back(
                    std::shared_ptr<Effect>(new TransferEffect(
                        pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                        std::vector<VkImage>(
                            pLogicalSwapchain->fakeImages.begin(),
                            pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
                        pLogicalSwapchain->images, pConfig)));
            }
        }

        VkImageView depthImageView =
            pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
        VkImage depthImage = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
        VkFormat depthFormat =
            pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;

        Logger::debug("effect string count: " + std::to_string(effectStrings.size()));
        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        for (uint32_t ei = 0; ei < pLogicalSwapchain->effects.size(); ei++) {
            pLogicalSwapchain->effects[ei]->useDepthImage(depthImageView);
            pLogicalSwapchain->effects[ei]->setChainPosition(ei == 0, ei == pLogicalSwapchain->effects.size() - 1);
        }

        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        if (swapchain != VK_NULL_HANDLE) {
            Logger::debug(
                "allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size())
                + " for swapchain " + convertToString(swapchain));
        } else {
            Logger::debug(
                "allocated CommandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size())
                + " for swapchain (rebuild)");
        }

        // Always create compute passes & FrameAnalyzer so it's available when the AutoHDR tab is opened.
        // FrameAnalyzer starts disabled to save GPU cycles until the user opens the tab.
        {
            std::vector<VkImage> computeSrcImages;
            VkFormat analyzerFormat;
            VkColorSpaceKHR analyzerColorSpace;

            // Now that the loop is done, set computeSrcSlice based on the tracked state
            pLogicalSwapchain->computeSrcSlice =
                pLogicalSwapchain->lastEffectWroteToReal ? 0 : pLogicalSwapchain->finalFakeSlice;

            if (pLogicalSwapchain->lastEffectWroteToReal) {
                // Reading from real swapchain images (already in destFormat/destColorSpace)
                computeSrcImages   = pLogicalSwapchain->images;
                analyzerFormat     = pLogicalSwapchain->format;
                analyzerColorSpace = pLogicalSwapchain->colorSpace;
            } else {
                // Reading from fake images (which are in sourceFormat/sourceColorSpace)
                uint32_t srcSlice = pLogicalSwapchain->computeSrcSlice;
                computeSrcImages  = std::vector<VkImage>(
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * srcSlice,
                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (srcSlice + 1));
                analyzerFormat     = pLogicalSwapchain->sourceFormat;
                analyzerColorSpace = pLogicalSwapchain->sourceColorSpace;
            }

            auto fa = std::make_shared<FrameAnalyzer>(
                pLogicalDevice, pLogicalSwapchain->imageExtent, computeSrcImages, analyzerFormat, analyzerColorSpace);
            // Start disabled. The overlay UI will enable it when the Auto HDR tab is opened.
            fa->setEnabled(false);
            pLogicalSwapchain->computePasses.push_back(fa);
            Logger::debug("created compute passes (FrameAnalyzer)");
        }

        writeCommandBuffers(
            pLogicalDevice, pLogicalSwapchain, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat,
            pLogicalSwapchain->commandBuffersEffect);
        Logger::debug("wrote CommandBuffers");

        // Only create semaphores on initial setup. Rebuilds preserve them as imageCount is fixed for a given swapchain.
        if (pLogicalSwapchain->semaphores.empty()) {
            pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
            if (pLogicalSwapchain->semaphores.size() != pLogicalSwapchain->imageCount) {
                Logger::err(
                    "Failed to create all semaphores: expected " + std::to_string(pLogicalSwapchain->imageCount)
                    + ", got " + std::to_string(pLogicalSwapchain->semaphores.size()));
            }
            Logger::debug("created semaphores");
        }

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++) {
            Logger::debug(
                std::to_string(i) + " written commandbuffer "
                + convertToString(pLogicalSwapchain->commandBuffersEffect[i]));
        }

        // Default chain for when effects are toggled off, preserved across rebuilds. When Auto HDR is active, we must still convert SDR->HDR even with effects off, lest the display receives SDR values in an HDR colorspace (teal/cyan artifact).
        if (!pLogicalSwapchain->defaultTransfer && !pLogicalSwapchain->defaultHdrEffect) {
            buildDefaultNoEffectChain(pLogicalDevice, pLogicalSwapchain, pConfig);
        }

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++) {
            Logger::debug(
                std::to_string(i) + " written noEffect commandbuffer "
                + convertToString(pLogicalSwapchain->commandBuffersNoEffect[i]));
        }

        // Passthrough eligible when no real processing is needed: No effects configured, OR effects are bypassed and VRAM was reclaimed (chain is just a fallback transfer)& no HDR output effect is needed
        pLogicalSwapchain->passthroughEligible =
            (effectStrings.empty() || bypassedAndReclaimed) && !shouldAppendHdrOutput;

        // Initialize ImGui Overlay eagerly while the queue is idle, the overlay renders on top of the real swapchain images, so it must use destFormat (HDR), not sourceFormat (SDR).
        if (swapchain != VK_NULL_HANDLE) {
            VkFormat overlayFormat = convertToUNORM(pLogicalSwapchain->destFormat);
            overlayManager.initOverlay(pLogicalDevice, pLogicalSwapchain, swapchain, overlayFormat, pConfig);
        }

        // Save pipeline cache only on initial setup so that we don't write to disk for every slider adjustment. The cache is already saved in vkBasalt_DestroyDevice for normal exits.
        if (swapchain != VK_NULL_HANDLE) {
            savePipelineCacheData(
                pLogicalDevice->device, pLogicalDevice->vkd, pLogicalDevice->pipelineCache,
                pLogicalDevice->pipelineCachePath);
        }
    }

    void rebuildEffectChain(
        LogicalDevice* pLogicalDevice,
        LogicalSwapchain* pLogicalSwapchain,
        Config* pConfig,
        OverlayManager& overlayManager,
        bool waitForIdle)
    {
        Logger::debug("Rebuilding effects for swapchain...");

        // Whether the configured chain is inert right now (effects toggled off and VRAM already reclaimed). getRequiredFakeSlices
        // needs it. While bypassed, the active chain is just a slice 0 transfer regardless of what "effects" holds.
        bool bypassedAndReclaimed = !g_effectsEnabled.load() && g_vramReclaimedForCurrentBypass;

        // Detect AutoHDR config changes that require a full swapchain rebuild. The real swapchain format is fixed at creation time, so toggling AutoHDR must too.
        ColorSpaceMode srcCsm = getColorSpaceMode(pLogicalSwapchain->sourceFormat, pLogicalSwapchain->sourceColorSpace);
        auto autoHdrOpt       = pConfig->getOption<std::string>("autoHdr", "on");
        bool autoHdrConfigEnabled = (autoHdrOpt == "on" || autoHdrOpt == "true" || autoHdrOpt == "1");
        bool forceHdrPgo          = (std::getenv("VKBASALT_FORCE_HDR_PGO") != nullptr);
        if (srcCsm == ColorSpaceMode::SDR_SRGB
            && (autoHdrConfigEnabled || forceHdrPgo) != pLogicalSwapchain->autoHdrActive) {
            Logger::debug("AutoHDR config changed, forcing swapchain rebuild to apply format change...");
            pLogicalSwapchain->forceSwapchainRebuild = true;

            // Reset passthrough state so the next CreateSwapchainKHR allocates fake images.
            pLogicalSwapchain->passthroughActive      = false;
            pLogicalSwapchain->passthroughTimerActive = false;
            pLogicalSwapchain->passthroughEligible    = false;

            if (!g_effectsEnabled.load() && !g_vramReclaimedForCurrentBypass) {
                g_effectsDisabledTime = std::chrono::steady_clock::now();
            }
            return;
        }

        // Always wait for the GPU to finish before freeing or resetting command buffers. Hot reloads already cause a stutter.
        // QueueWaitIdle is acceptable and prevents VUID-vkFreeCommandBuffers-pCommandBuffers-00047 and VUID-vkResetCommandBuffer-commandBuffer-00045.
        (void) waitForIdle; // Suppress unused parameter warning if we ignore it
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        auto effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {});
        pruneEmptyEffectNames(effectStrings);
        bool hdrOutputNeeded = isHdrOutputNeeded(pConfig, pLogicalSwapchain);

        uint32_t requiredSlices = getRequiredFakeSlices(
            pLogicalDevice, pLogicalSwapchain->format, effectStrings, hdrOutputNeeded, bypassedAndReclaimed);
        uint32_t requiredFakeImageCount = pLogicalSwapchain->imageCount * requiredSlices;

        // If the fake image pool is empty (e.g., passthrough mode reclaimed VRAM), Can't build any command buffers. Force a swapchain rebuild to reallocate the pool.
        if (pLogicalSwapchain->fakeImages.empty()) {
            Logger::debug("Effect pool is empty (passthrough active). Forcing swapchain rebuild to reallocate...");
            pLogicalSwapchain->passthroughActive      = false;
            pLogicalSwapchain->passthroughTimerActive = false;
            pLogicalSwapchain->forceSwapchainRebuild  = true;
            pLogicalSwapchain->passthroughEligible    = false;
            return;
        }

        // Chain GREW beyond allocated pool, game holds old VkImage handles, so we must force the game to recreate its swapchain.
        if (requiredFakeImageCount > pLogicalSwapchain->fakeImages.size()) {
            Logger::debug("Effect chain grew beyond allocated pool. Forcing swapchain rebuild...");
            pLogicalSwapchain->forceSwapchainRebuild = true;
            pLogicalSwapchain->passthroughEligible   = false;

            // Clear current effects to prevent using them with the undersized image pool.
            // Skip rebuildFallbackChain here to avoid QueueWaitIdle deadlocks that can occur when the game is unresponsive
            // or in a weird state. The game will recreate the swapchain, at which point a fresh, correctly sized chain will be built.
            pLogicalSwapchain->effects.clear();

            // Move compute passes to the graveyard instead of destroying them immediately.
            // The overlay may still hold scope texture descriptor sets referencing images owned by the FrameAnalyzer.
            // Destroying them now would cause use after free when the overlay renders its next frame.
            for (auto& pass : pLogicalSwapchain->computePasses) {
                pLogicalSwapchain->computePassGraveyard.push_back(pass);
            }
            pLogicalSwapchain->computePasses.clear();

            trimComputePassGraveyard(pLogicalDevice, pLogicalSwapchain);

            pLogicalSwapchain->nitCalibrationEffect = nullptr;

            return; // Wait for the game to handle VK_ERROR_OUT_OF_DATE_KHR
        }

        // Chain SHRUNK significantly (wasting at least 1 full slice of VRAM). Force rebuild to reclaim.
        if (requiredFakeImageCount < pLogicalSwapchain->fakeImages.size()) {
            uint32_t currentSlices = pLogicalSwapchain->fakeImages.size() / pLogicalSwapchain->imageCount;
            if (currentSlices > requiredSlices) {
                Logger::debug(
                    "Effect chain shrunk, wasting " + std::to_string(currentSlices - requiredSlices)
                    + " slices. Forcing swapchain rebuild to reclaim VRAM...");
                pLogicalSwapchain->forceSwapchainRebuild = true;
                // Passthrough is safe only when nothing at all needs doing: one slice, no normal effects and no HDR output.
                // The old totalEffectCount == 0 test folded HDR in through calculateTotalEffectCount; keep that meaning explicit.
                pLogicalSwapchain->passthroughEligible =
                    (requiredSlices == 1 && effectStrings.empty() && !hdrOutputNeeded);

                rebuildFallbackChain(pLogicalDevice, pLogicalSwapchain, pConfig);
                return; // Wait for the game to handle VK_ERROR_OUT_OF_DATE_KHR
            }
        }

        // Chain same size, rebuild in-place using existing pool. The game's cached VkImage handles remain valid.
        Logger::debug("Effect chain fits in existing pool. Rebuilding in-place...");

        // Reset the default chain so it picks up new config values (tone mapper, white point, peak nits, etc.)
        pLogicalSwapchain->defaultTransfer.reset();
        pLogicalSwapchain->defaultHdrEffect.reset();

        // Move old compute passes to the graveyard instead of destroying them immediately.
        for (auto& pass : pLogicalSwapchain->computePasses) {
            pLogicalSwapchain->computePassGraveyard.push_back(pass);
        }

        trimComputePassGraveyard(pLogicalDevice, pLogicalSwapchain);

        if (!pLogicalSwapchain->commandBuffersEffect.empty()) {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersEffect.size(),
                pLogicalSwapchain->commandBuffersEffect.data());
            pLogicalSwapchain->commandBuffersEffect.clear();
        }
        pLogicalSwapchain->effects.clear();
        pLogicalSwapchain->computePasses.clear();
        buildEffectChain(pLogicalDevice, pLogicalSwapchain, VK_NULL_HANDLE, pConfig, overlayManager);
        Logger::debug("Rebuild complete.");
    }
} // namespace vkBasalt
