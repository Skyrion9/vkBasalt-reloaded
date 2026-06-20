#include "keyboard_input.hpp"
#include "logger.hpp"

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
    static bool is_wayland = false;
    static bool input_initialized = false;

    static void init_input_backend()
    {
        if (input_initialized) return;
        
        const char* wayland_var = getenv("WAYLAND_DISPLAY");
        const char* x11_var = getenv("DISPLAY");
         
        // Xwayland ıs also an X11 backend (XQueryKeymap).
        if (wayland_var && strcmp(wayland_var, "") != 0 && x11_var && strcmp(x11_var, "") != 0)
        {
#if VKBASALT_X11
            Logger::debug("Detected XWayland. Using X11 input backend.");
            is_wayland = false;
#endif
        }
        // Wayland game, The Vulkan hook (vkCreateWaylandSurfaceKHR in basalt.cpp) will initialize the backend.
        else if (wayland_var && strcmp(wayland_var, "") != 0)
        {
#if VKBASALT_WAYLAND
            Logger::debug("Detected native Wayland. Waiting for Vulkan surface hook to provide wl_display.");
            is_wayland = true;
            // initWaylandInput() is called from basalt.cpp when the surface is created.
#endif
        }
        // Pure X11
        else if (x11_var && strcmp(x11_var, "") != 0)
        {
#if VKBASALT_X11
            Logger::debug("Detected X11. Using X11 input backend.");
            is_wayland = false;
#endif
        }
        
        input_initialized = true;
    }

    uint32_t convertToKeySym(std::string key)
    {
        init_input_backend();
        
#if VKBASALT_WAYLAND
        if (is_wayland) return convertToKeySymWayland(key);
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
        if (is_wayland) return isKeyPressedWayland(ks);
#endif

#if VKBASALT_X11
        return isKeyPressedX11(ks);
#endif
        return false;
    }
} // namespace vkBasalt
