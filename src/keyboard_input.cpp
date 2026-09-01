#include "keyboard_input.hpp"
#include "logger.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef VKBASALT_X11
#define VKBASALT_X11 1
#endif

#ifndef VKBASALT_WAYLAND
#define VKBASALT_WAYLAND 1
#endif

#if VKBASALT_X11
#include "keyboard_input_x11.hpp"
#endif

#if VKBASALT_WAYLAND
#include "keyboard_input_wayland.hpp"
#endif

namespace vkBasalt
{
    static bool is_wayland        = false;
    static bool input_initialized = false;

    static void init_input_backend()
    {
        if (input_initialized)
            return;
        const char* wayland_var = getenv("WAYLAND_DISPLAY");
        const char* x11_var     = getenv("DISPLAY");

        // Fallback logic if no surface was created before the first key check
        if (wayland_var && strcmp(wayland_var, "") != 0)
        {
#if VKBASALT_WAYLAND
            Logger::debug("Wayland session detected via env. Waiting for surface hook.");
            is_wayland = true;
#endif
        }
        else if (x11_var && strcmp(x11_var, "") != 0)
        {
#if VKBASALT_X11
            Logger::debug("Pure X11 session detected via env. Using X11 input backend.");
            is_wayland = false;
#endif
        }
        input_initialized = true;
    }

    // Called from surface creation hooks to confirm/override the backend
    void setInputBackend(bool wayland)
    {
        // X11 surfaces strongly imply XWayland or native X11. XWayland games (like Naraka) often create both Wayland (for popups/launchers)
        // and X11 (for the main game window) surfaces. If an X11 surface is created, always prefer X11 to ensure main window input works.
        if (!wayland)
        {
            is_wayland = false;
        }
        else
        {
            // Only default to Wayland if X11 hasn't already claimed the backend
            if (!input_initialized || is_wayland)
            {
                is_wayland = true;
            }
        }
        input_initialized = true;
        Logger::debug(std::string("Input backend confirmed: ") + (is_wayland ? "Wayland" : "X11"));
    }

    // Shared scale detection for Env vars and KDE config files. Used by both Wayland and X11 backends to avoid DRY violations across the static library boundary.
    float getScaleFromEnvAndKDE() {
        const char* qtScale = std::getenv("QT_SCALE_FACTOR");
        if (qtScale) {
            float s = (float)std::atof(qtScale);
            if (s >= 0.5f && s <= 5.0f) return s;
        }
        const char* gdkScale = std::getenv("GDK_SCALE");
        if (gdkScale) {
            float s = (float)std::atof(gdkScale);
            const char* gdkDpi = std::getenv("GDK_DPI_SCALE");
            if (gdkDpi) s *= (float)std::atof(gdkDpi);
            if (s >= 0.5f && s <= 5.0f) return s;
        }

        const char* home = std::getenv("HOME");
        if (!home) return 0.0f;
        std::string homeStr(home);
        std::vector<std::string> paths = {
            homeStr + "/.config/kdeglobals",
            homeStr + "/.config/kwinrc",
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (!f.good()) continue;
            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind("ScreenScaleFactors=", 0) == 0) {
                    std::string val = line.substr(19);
                    size_t comma = val.find(',');
                    if (comma != std::string::npos) val = val.substr(0, comma);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) return s;
                }
                if (line.rfind("ScaleFactor=", 0) == 0) {
                    std::string val = line.substr(12);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) return s;
                }
                if (line.rfind("Scale=", 0) == 0) {
                    std::string val = line.substr(6);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) return s;
                }
            }
        }
        return 0.0f;
    }

    // Returns true if the active input backend is Wayland
    bool isWaylandBackend()
    {
        init_input_backend();
        return is_wayland;
    }

    uint32_t convertToKeySym(const std::string& key)
    {
        init_input_backend();

#if VKBASALT_WAYLAND
        if (is_wayland)
            return convertToKeySymWayland(key);
#endif

#if VKBASALT_X11
        return convertToKeySymX11(key);
#endif
        return 0u;
    }

    bool isKeyPressed(uint32_t ks)
    {
        init_input_backend();

#if VKBASALT_WAYLAND
        if (is_wayland)
            return isKeyPressedWayland(ks);
#endif

#if VKBASALT_X11
        return isKeyPressedX11(ks);
#endif
        return false;
    }
} // namespace vkBasalt
