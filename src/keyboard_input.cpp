#include "keyboard_input.hpp"
#include "logger.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

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
