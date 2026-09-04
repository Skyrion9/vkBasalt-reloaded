#include "keyboard_input_x11.hpp"

#include <cstdlib>
#include <cstdint>

#include <X11/X.h>
#include <imgui.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <dlfcn.h>

// Undefine X11 macros that collide with identifiers in the rest of the codebase (e.g. logger.hpp has an enum member called None, which X11/X.h #defines as 0L)
#undef None

#include "keyboard_input.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    static Display* g_gameDisplay = nullptr;
    static Window g_gameWindow = 0;
    static Display* g_fallbackDisplay = nullptr; // Cached for Wayland games in Gamescope

    // XInput2 dynamic loading for scroll wheel polling
    typedef Status (*PFN_XIQueryVersion)(Display*, int*, int*);
    typedef int (*PFN_XISelectEvents)(Display*, Window, XIEventMask*, int);

    static void* s_libXi = nullptr;
    static PFN_XIQueryVersion s_XIQueryVersion = nullptr;
    static PFN_XISelectEvents s_XISelectEvents = nullptr;

    static Display* g_scrollDisplay = nullptr;
    static int xi2_opcode = 0;

    static bool initX11Scroll() {
        if (g_scrollDisplay) return true;
        if (s_libXi == nullptr) {
            s_libXi = dlopen("libXi.so.6", RTLD_LAZY | RTLD_NOLOAD);
            if (!s_libXi) s_libXi = dlopen("libXi.so.6", RTLD_LAZY);
            if (!s_libXi) {
                s_libXi = (void*)-1; // Mark as failed to avoid retrying
                return false;
            }
            s_XIQueryVersion = (PFN_XIQueryVersion)dlsym(s_libXi, "XIQueryVersion");
            s_XISelectEvents = (PFN_XISelectEvents)dlsym(s_libXi, "XISelectEvents");
        }
        if (s_libXi == (void*)-1 || !s_XIQueryVersion || !s_XISelectEvents) return false;

        const char* disVar = getenv("DISPLAY");
        if (!disVar) return false;

        // Open a dedicated connection so our event queue is completely separate from the game's
        g_scrollDisplay = XOpenDisplay(disVar);
        if (!g_scrollDisplay) return false;

        int event, error;
        if (!XQueryExtension(g_scrollDisplay, "XInputExtension", &xi2_opcode, &event, &error)) {
            XCloseDisplay(g_scrollDisplay);
            g_scrollDisplay = nullptr;
            return false;
        }

        int major = 2, minor = 0;
        if (s_XIQueryVersion(g_scrollDisplay, &major, &minor) == BadRequest) {
            XCloseDisplay(g_scrollDisplay);
            g_scrollDisplay = nullptr;
            return false;
        }

        Window root = DefaultRootWindow(g_scrollDisplay);
        unsigned char mask_bits[XIMaskLen(XI_RawButtonPress)] = { 0 };
        XISetMask(mask_bits, XI_RawButtonPress);

        XIEventMask evmask;
        evmask.deviceid = XIAllMasterDevices;
        evmask.mask_len = sizeof(mask_bits);
        evmask.mask = mask_bits;

        s_XISelectEvents(g_scrollDisplay, root, &evmask, 1);
        XFlush(g_scrollDisplay);
        Logger::debug("X11 Scroll: XInput2 RawButtonPress initialized on dedicated connection.");
        return true;
    }

    void initX11Input(void* display_ptr, void* window_ptr) {
        g_gameDisplay = (Display*)display_ptr;
        g_gameWindow = (Window)(uintptr_t)window_ptr;
        Logger::debug("X11 Input: Piggybacked on game's Display and Window successfully.");
    }

    uint32_t convertToKeySymX11(std::string key) {
        return (uint32_t)XStringToKeysym(key.c_str());
    }

    bool isKeyPressedX11(uint32_t ks) {
        Display* dpy = g_gameDisplay;
        if (!dpy) {
            if (!g_fallbackDisplay) {
                const char* disVar = getenv("DISPLAY");
                if (!disVar) return false;
                g_fallbackDisplay = XOpenDisplay(disVar);
                if (!g_fallbackDisplay) return false;
            }
            dpy = g_fallbackDisplay;
        }
        
        char keys_return[32];
        XQueryKeymap(dpy, keys_return);
        KeyCode kc2 = XKeysymToKeycode(dpy, (KeySym)ks);
        return !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
    }

    float getX11UIScale() {
        // Cache the scale to prevent it from resetting to def during transient display  connection failures in the swapchain rebuild window (e.g., passthrough mode).
        static float s_cachedScale = -1.0f;
        if (s_cachedScale > 0.0f) {
            return s_cachedScale;
        }

        float sharedScale = getScaleFromEnvAndKDE();
        if (sharedScale > 0.0f) {
            s_cachedScale = sharedScale;
            return sharedScale;
        }

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

    void supplementX11MouseButtons() {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();

        Display* dpy = g_gameDisplay;
        if (!dpy) {
            if (!g_fallbackDisplay) {
                const char* disVar = getenv("DISPLAY");
                if (!disVar) return;
                g_fallbackDisplay = XOpenDisplay(disVar);
                if (!g_fallbackDisplay) return;
            }
            dpy = g_fallbackDisplay;
        }

        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        Window win = g_gameWindow ? g_gameWindow : DefaultRootWindow(dpy);
        if (XQueryPointer(dpy, win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
            // X11: Button2=Middle, Button3=Right. ImGui: 1=Right, 2=Middle.
            if (!io.MouseDown[1]) io.MouseDown[1] = (mask & Button3Mask) != 0;
            if (!io.MouseDown[2]) io.MouseDown[2] = (mask & Button2Mask) != 0;
        }
    }

    void updateX11ImGuiIO(bool overlayOpen, float scale) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        Display* dpy = g_gameDisplay;
        Window win = g_gameWindow;
        if (!dpy) {
            if (!g_fallbackDisplay) {
                const char* disVar = getenv("DISPLAY");
                if (!disVar) return;
                g_fallbackDisplay = XOpenDisplay(disVar);
                if (!g_fallbackDisplay) return;
            }
            dpy = g_fallbackDisplay;
            win = DefaultRootWindow(dpy);
        }
        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        if (XQueryPointer(dpy, win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
            io.MousePos = ImVec2((float)win_x * scale, (float)win_y * scale);
            io.MouseDown[0] = (mask & Button1Mask) != 0; // Left
            io.MouseDown[1] = (mask & Button3Mask) != 0; // Right
            io.MouseDown[2] = (mask & Button2Mask) != 0; // Middle
        }

        // Poll scroll wheel via XInput2 Raw Events on a dedicated connection.
        if (initX11Scroll()) {
            XEvent ev;
            while (XPending(g_scrollDisplay) > 0) {
                XNextEvent(g_scrollDisplay, &ev);
                if (ev.type == GenericEvent && ev.xcookie.extension == xi2_opcode) {
                    if (XGetEventData(g_scrollDisplay, &ev.xcookie)) {
                        if (ev.xcookie.evtype == XI_RawButtonPress) {
                            XIRawEvent* raw = (XIRawEvent*)ev.xcookie.data;
                            if (raw->detail == 4) io.MouseWheel += 1.0f;
                            else if (raw->detail == 5) io.MouseWheel -= 1.0f;
                        }
                        XFreeEventData(g_scrollDisplay, &ev.xcookie);
                    }
                }
            }
        }
    }
} // namespace vkBasalt
