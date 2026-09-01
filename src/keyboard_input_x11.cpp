#include "keyboard_input_x11.hpp"

#include <cstdlib>
#include <cstdint>

#include <X11/X.h>
#include <imgui.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

// Undefine X11 macros that collide with identifiers in the rest of the codebase (e.g. logger.hpp has an enum member called None, which X11/X.h #defines as 0L)
#undef None

#include "keyboard_input.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    static Display* g_gameDisplay = nullptr;
    static Window g_gameWindow = 0;

    void initX11Input(void* display_ptr, void* window_ptr) {
        g_gameDisplay = (Display*)display_ptr;
        g_gameWindow = (Window)(uintptr_t)window_ptr;
        Logger::debug("X11 Input: Piggybacked on game's Display and Window successfully.");
    }

    uint32_t convertToKeySymX11(std::string key) {
        uint32_t result = (uint32_t) XStringToKeysym(key.c_str());
        return result;
    }

    bool isKeyPressedX11(uint32_t ks) {
        Display* dpy = g_gameDisplay;
        if (!dpy) {
            const char* disVar = getenv("DISPLAY");
            if (!disVar) return false;
            dpy = XOpenDisplay(disVar);
            if (!dpy) return false;
        }
        
        char keys_return[32];
        XQueryKeymap(dpy, keys_return);
        KeyCode kc2 = XKeysymToKeycode(dpy, (KeySym) ks);
        bool pressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
        
        if (!g_gameDisplay && dpy) XCloseDisplay(dpy);
        return pressed;
    }

    float getX11UIScale() {
        // Cache the scale to prevent it from resetting to def during transient display  connection failures in the swapchain rebuild window (e.g., passthrough mode).
        static float s_cachedScale = -1.0f;
        if (s_cachedScale > 0.0f) {
            return s_cachedScale;
        }

        // Env vars and KDE config files (shared across backends)
        float sharedScale = getScaleFromEnvAndKDE();
        if (sharedScale > 0.0f) {
            s_cachedScale = sharedScale;
            return sharedScale;
        }

        // X11 Xft.dpi and physical screen size
        Display* dpy = g_gameDisplay;
        bool own = false;
        if (!dpy) { 
            const char* disVar = getenv("DISPLAY");
            if (disVar) dpy = XOpenDisplay(disVar); 
            own = true; 
        }
        
        float scale = 1.0f;
        if (dpy) {
            // Try XGetDefault (often fails under Wine/Proton because Xrm isn't initialized)
            char* dpi = XGetDefault(dpy, "Xft", "dpi");
            if (dpi) { 
                float d = (float)std::atof(dpi); 
                if (d > 0.0f) scale = d / 96.0f; 
            }
            
            // Fallback: calculate DPI from physical screen dimensions
            if (scale == 1.0f) {
                int screen = DefaultScreen(dpy);
                int heightPx = DisplayHeight(dpy, screen);
                int heightMM = DisplayHeightMM(dpy, screen);
                if (heightMM > 0) {
                    double dpiCalc = (double)heightPx / ((double)heightMM / 25.4);
                    scale = (float)(dpiCalc / 96.0);
                    // XWayland sometimes reports bogus physical sizes. Clamp to reasonable bounds.
                    if (scale < 0.5f || scale > 5.0f) scale = 1.0f;
                }
            }
            if (own) XCloseDisplay(dpy);
        }
        
        s_cachedScale = scale;
        return scale;
    }

    void updateX11ImGuiIO(bool overlayOpen, float scale) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        
        Display* dpy = g_gameDisplay;
        Window win = g_gameWindow;
        if (!dpy) {
            dpy = XOpenDisplay(getenv("DISPLAY"));
            if (!dpy) return;
            win = DefaultRootWindow(dpy);
        }

        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        if (XQueryPointer(dpy, win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
            io.MousePos = ImVec2((float)win_x * scale, (float)win_y * scale);
            io.MouseDown[0] = (mask & Button1Mask) != 0;
            io.MouseDown[1] = (mask & Button2Mask) != 0;
            io.MouseDown[2] = (mask & Button3Mask) != 0;
        }
        
        if (!g_gameDisplay && dpy) XCloseDisplay(dpy);
    }
} // namespace vkBasalt
