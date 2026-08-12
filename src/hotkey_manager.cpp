#include "hotkey_manager.hpp"
#include "overlay_manager.hpp"
#include "effect_chain.hpp"
#include "config.hpp"
#include "keyboard_input.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include "keyboard_input_wayland.hpp"
#endif

#include <memory>
#include <unordered_map>

namespace vkBasalt {

    bool processHotkeysAndReloads(
        std::shared_ptr<Config>& pConfig,
        std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>>& swapchainMap,
        OverlayManager& overlayManager)
    {
        static uint32_t keySymbol        = convertToKeySym(pConfig->getOption<std::string>("toggleKey", "Insert"));
        static uint32_t reloadKeySymbol  = convertToKeySym(pConfig->getOption<std::string>("reloadConfigKey", "End"));
        static uint32_t overlayKeySymbol = convertToKeySym(pConfig->getOption<std::string>("overlayToggleKey", "Home"));
        static std::string cachedToggleKey, cachedReloadKey, cachedOverlayKey;
        static bool pressed       = false;
        static bool reloadPressed = false;
        static bool overlayPressed = false;
        static bool presentEffect = pConfig->getOption<bool>("enableOnLaunch", true);
        static bool skipNextPresent = false;

        g_effectsEnabled = presentEffect;

        // Helpers
        auto reloadConfig = [&]() {
            pConfig = std::make_shared<Config>();
            keySymbol        = convertToKeySym(pConfig->getOption<std::string>("toggleKey", "Insert"));
            reloadKeySymbol  = convertToKeySym(pConfig->getOption<std::string>("reloadConfigKey", "End"));
            overlayKeySymbol = convertToKeySym(pConfig->getOption<std::string>("overlayToggleKey", "Home"));
            presentEffect    = pConfig->getOption<bool>("enableOnLaunch", true);
            g_effectsEnabled = presentEffect;
            
            // Sync caches to prevent the refresh block below from desync
            cachedToggleKey  = pConfig->getOption<std::string>("toggleKey", "Insert");
            cachedReloadKey  = pConfig->getOption<std::string>("reloadConfigKey", "End");
            cachedOverlayKey = pConfig->getOption<std::string>("overlayToggleKey", "Home");
        };

        auto rebuildAll = [&]() {
            for (auto& pair : swapchainMap) {
                rebuildEffectChain(pair.second->pLogicalDevice, pair.second.get(), pConfig.get(), overlayManager);
            }
        };

        // Refresh cached keysyms whenever the config keybind values changes in memory
        {
            std::string tk = pConfig->getOption<std::string>("toggleKey", "Insert");
            std::string rk = pConfig->getOption<std::string>("reloadConfigKey", "End");
            std::string ok = pConfig->getOption<std::string>("overlayToggleKey", "Home");
            if (tk != cachedToggleKey)  { keySymbol = convertToKeySym(tk);        cachedToggleKey = tk; }
            if (rk != cachedReloadKey)  { reloadKeySymbol = convertToKeySym(rk);  cachedReloadKey = rk; }
            if (ok != cachedOverlayKey) { overlayKeySymbol = convertToKeySym(ok); cachedOverlayKey = ok; }
        }

        // Check if any overlay is in keybinding mode (suppress all hotkeys)
        bool anyBinding = overlayManager.anyBinding();

        if (!anyBinding && isKeyPressed(keySymbol)) {
            if (!pressed) {
                presentEffect = !presentEffect;
                g_effectsEnabled = presentEffect;
                pressed = true;
                Logger::debug(presentEffect ? "vkBasalt effects enabled" : "vkBasalt effects disabled");
            }
        } else {
            pressed = false;
        }

        if (!anyBinding && isKeyPressed(reloadKeySymbol)) {
            if (!reloadPressed) {
                reloadPressed = true;
                skipNextPresent = true;
                Logger::debug("Reloading vkBasalt config...");
                reloadConfig();
                rebuildAll();
                overlayManager.updateAllOverlays(pConfig.get(), true);
                Logger::debug("vkBasalt config reloaded successfully!");
            }
        } else {
            reloadPressed = false;
        }

        if (!anyBinding && isKeyPressed(overlayKeySymbol)) {
            if (!overlayPressed) {
                overlayPressed = true;
                overlayManager.toggleAllOverlays();
            }
        } else {
            overlayPressed = false;
        }

        // Skip present (from End key reload)
        if (skipNextPresent) {
            skipNextPresent = false;
            Logger::debug("Skipping present frame to allow layout stabilization...");
            return true;
        }

        // Preview reload (in-memory config, no disk read)
        if (g_triggerPreviewReload.exchange(false)) {
            Logger::debug("Live preview rebuild (in-memory config)...");
            rebuildAll();
            Logger::debug("Preview rebuild complete.");
            return true;
        }

        // Revert reload (disk read, keep overlay open)
        if (g_triggerRevertReload.exchange(false)) {
            Logger::debug("Revert: reloading config from disk...");
            reloadConfig();
            rebuildAll();
            overlayManager.updateAllOverlays(pConfig.get(), false);
            Logger::debug("Revert complete. Config restored from disk.");
            return true;
        }

        // Soft reload (disk read, keep overlay open)
        if (g_triggerSoftReload.exchange(false)) {
            Logger::debug("ImGui requested soft reload (params only)...");
            reloadConfig();
            rebuildAll();
            overlayManager.updateAllOverlays(pConfig.get(), false);
            Logger::debug("ImGui soft reload complete. Overlay remains open.");
            return true;
        }

        // Hot reload (disk read, close overlay, full reinit)
        if (g_triggerHotReload.exchange(false)) {
            Logger::debug("ImGui requested hot reload via UI...");
            reloadConfig();
            overlayManager.closeAllOverlays();
            rebuildAll();
            overlayManager.updateAllOverlays(pConfig.get(), true);
            Logger::debug("ImGui hot reload complete.");
            return true;
        }

        return false; // No skip necessary, continue rendering normally
    }

} // namespace vkBasalt
