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

#ifndef VKBASALT_WAYLAND
#define VKBASALT_WAYLAND 1
#endif
#if VKBASALT_WAYLAND
#include "keyboard_input_wayland.hpp"
#endif

namespace vkBasalt
{
    static Display* g_gameDisplay     = nullptr;
    static Window g_gameWindow        = 0;
    static Display* g_targetDisplay   = nullptr;
    static Window g_targetWindow      = 0;
    static Display* g_fallbackDisplay = nullptr; // Cached for Wayland games in Gamescope

    // XInput2 dynamic loading for scroll wheel polling
    using PFN_XIQueryVersion = Status (*)(Display*, int*, int*);
    using PFN_XISelectEvents = int (*)(Display*, Window, XIEventMask*, int);

    static void* s_libXi                       = nullptr;
    static PFN_XIQueryVersion s_XIQueryVersion = nullptr;
    static PFN_XISelectEvents s_XISelectEvents = nullptr;

    static Display* g_scrollDisplay = nullptr;
    static int xi2_opcode           = 0;

    static bool initX11Scroll()
    {
        if (g_scrollDisplay) return true;
        if (s_libXi == nullptr) {
            s_libXi = dlopen("libXi.so.6", RTLD_LAZY | RTLD_NOLOAD);
            if (!s_libXi) s_libXi = dlopen("libXi.so.6", RTLD_LAZY);
            if (!s_libXi) {
                s_libXi = (void*) -1; // Mark as failed to avoid retrying
                return false;
            }
            s_XIQueryVersion = reinterpret_cast<PFN_XIQueryVersion>(dlsym(s_libXi, "XIQueryVersion"));
            s_XISelectEvents = reinterpret_cast<PFN_XISelectEvents>(dlsym(s_libXi, "XISelectEvents"));
        }
        if (s_libXi == (void*) -1 || !s_XIQueryVersion || !s_XISelectEvents) return false;

        const char* disVar = getenv("DISPLAY");
        if (!disVar) return false;

        // Open a dedicated connection so our event queue is completely separate from the game's
        g_scrollDisplay = XOpenDisplay(disVar);
        if (!g_scrollDisplay) return false;

        int event = 0, error = 0;
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

        Window root                                           = DefaultRootWindow(g_scrollDisplay);
        unsigned char mask_bits[XIMaskLen(XI_RawButtonPress)] = {0};
        XISetMask(mask_bits, XI_RawButtonPress);

        XIEventMask evmask;
        evmask.deviceid = XIAllMasterDevices;
        evmask.mask_len = sizeof(mask_bits);
        evmask.mask     = mask_bits;

        s_XISelectEvents(g_scrollDisplay, root, &evmask, 1);
        XFlush(g_scrollDisplay);
        Logger::debug("X11 Scroll: XInput2 RawButtonPress initialized on dedicated connection.");
        return true;
    }

    void initX11Input(void* display_ptr, void* window_ptr)
    {
        g_gameDisplay = static_cast<Display*>(display_ptr);
        g_gameWindow  = static_cast<Window>((uintptr_t) window_ptr);
        Logger::debug("X11 Input: Piggybacked on game's Display and Window successfully.");
    }

    void setX11InputTarget(void* display_ptr, void* window_ptr)
    {
        g_targetDisplay = static_cast<Display*>(display_ptr);
        g_targetWindow  = static_cast<Window>((uintptr_t) window_ptr);
    }

    bool isX11PointerAvailable()
    {
        return g_targetDisplay || g_gameDisplay || g_fallbackDisplay || getenv("DISPLAY");
    }

    uint32_t convertToKeySymX11(const std::string& key)
    {
        return static_cast<uint32_t>(XStringToKeysym(key.c_str()));
    }

    bool isKeyPressedX11(uint32_t ks)
    {
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
        KeyCode kc2 = XKeysymToKeycode(dpy, static_cast<KeySym>(ks));
        return !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
    }

    float getX11UIScale()
    {
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
        bool own     = false;
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
                auto d = static_cast<float>(std::atof(dpi));
                if (d > 0.0f) scale = d / 96.0f;
            }

            // Fallback: calculate DPI from physical screen dimensions
            if (scale == 1.0f) {
                int screen   = DefaultScreen(dpy);
                int heightPx = DisplayHeight(dpy, screen);
                int heightMM = DisplayHeightMM(dpy, screen);
                if (heightMM > 0) {
                    double dpiCalc = static_cast<double>(heightPx) / (static_cast<double>(heightMM) / 25.4);
                    scale          = static_cast<float>(dpiCalc / 96.0);
                    // XWayland sometimes reports bogus physical sizes. Clamp to reasonable bounds.
                    if (scale < 0.5f || scale > 5.0f) scale = 1.0f;
                }
            }
            if (own) XCloseDisplay(dpy);
        }

        s_cachedScale = scale;
        return scale;
    }

    void supplementX11MouseButtons()
    {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io  = ImGui::GetIO();
        Display* dpy = g_targetDisplay ? g_targetDisplay : g_gameDisplay;
        if (!dpy) {
            if (!g_fallbackDisplay) {
                const char* disVar = getenv("DISPLAY");
                if (!disVar) return;
                g_fallbackDisplay = XOpenDisplay(disVar);
                if (!g_fallbackDisplay) return;
            }
            dpy = g_fallbackDisplay;
        }

        Window win = g_gameWindow;
        if (win == 0) {
            Window focus_win = 0;
            int revert_to    = 0;
            XGetInputFocus(dpy, &focus_win, &revert_to);
            if (focus_win != 0 && focus_win != PointerRoot) {
                win = focus_win;
            } else {
                win = DefaultRootWindow(dpy);
            }
        }

        Window root = 0, child = 0;
        int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
        unsigned int mask = 0;
        if (XQueryPointer(dpy, win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
            // X11: Button2=Middle, Button3=Right. ImGui: 1=Right, 2=Middle.
            if (!io.MouseDown[1]) io.MouseDown[1] = (mask & Button3Mask) != 0;
            if (!io.MouseDown[2]) io.MouseDown[2] = (mask & Button2Mask) != 0;
        }
    }

    void updateX11ImGuiIO(bool /*overlayOpen*/, float scale)
    {
        if (!ImGui::GetCurrentContext()) return;

        // Under Proton/winewayland, prefer the Wine safe, pointer only Wayland queue if it has valid data.
        // This keeps keyboard on X11 while restoring accurate surface relative mouse coordinates.
        bool isWineEnv =
            (getenv("WINELOADER") != nullptr || getenv("WINEDLLPATH") != nullptr || getenv("WINEPREFIX") != nullptr
             || getenv("STEAM_COMPAT_DATA_PATH") != nullptr);

#if VKBASALT_WAYLAND
        if (isWineEnv) {
            float waylandScale = getWaylandUIScale();
            if (waylandScale < 0.5f) waylandScale = scale;
            if (updateWaylandPointerImGuiIO(waylandScale)) {
                return;
            }
        }
#endif

        ImGuiIO& io  = ImGui::GetIO();
        Display* dpy = g_targetDisplay ? g_targetDisplay : g_gameDisplay;
        Window win   = g_targetWindow ? g_targetWindow : g_gameWindow;

        if (!dpy) {
            if (!g_fallbackDisplay) {
                const char* disVar = getenv("DISPLAY");
                if (!disVar) return;
                g_fallbackDisplay = XOpenDisplay(disVar);
                if (!g_fallbackDisplay) return;
            }
            dpy = g_fallbackDisplay;
        }

        // Determine the query window.Under Wine in WAYLAND mode (g_gameWindow == 0), no X11 window exists for the game.
        // So we must fall back to the root window for absolute screen coordinates.
        // Under Wine in X11 mode (g_gameWindow != 0), the game has a real X11 window.
        // So we must use window relative coordinates via XQueryPointer on that window.
        if (win == 0) {
            if (isWineEnv) {
                // No X11 surface was created (Wayland mode under winewayland.drv).
                // Use root window for absolute screen coordinates.
                win = DefaultRootWindow(dpy);
            } else {
                Window focus_win = 0;
                int revert_to    = 0;
                XGetInputFocus(dpy, &focus_win, &revert_to);
                if (focus_win != 0 && focus_win != PointerRoot) {
                    win = focus_win;
                } else {
                    win = DefaultRootWindow(dpy);
                }
            }
        }

        Window root = 0, child = 0;
        int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
        unsigned int mask = 0;

        bool useRootCoords = (win == DefaultRootWindow(dpy));
        bool havePointer   = XQueryPointer(dpy, win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);

        if (!havePointer) {
            // XQueryPointer fails if the window died (CEF recreates its host windows) or lives on
            // another screen. Root is serverwide and always queryable, so fall back to it rather
            // than leaving io.MousePos at -FLT_MAX, which makes ImGui drop the cursor entirely.
            Window rootChild = 0;
            havePointer =
                XQueryPointer(dpy, DefaultRootWindow(dpy), &root, &rootChild, &root_x, &root_y, &win_x, &win_y, &mask);
            useRootCoords = true;
        }

        static uint32_t s_ptrLogCounter = 0;
        if ((s_ptrLogCounter++ % 60) == 0) {
            Logger::trace(
                "X11 pointer: win=" + std::to_string(win) + " have=" + (havePointer ? "1" : "0")
                + " root=" + std::to_string(root_x) + "," + std::to_string(root_y) + " winxy=" + std::to_string(win_x)
                + "," + std::to_string(win_y) + " useRoot=" + (useRootCoords ? "1" : "0"));
        }

        if (havePointer) {
            // XQueryPointer already reports win_x/win_y relative to the queried window.
            // Only the root fallback needs absolute coordinates.
            float mx = static_cast<float>(useRootCoords ? root_x : win_x);
            float my = static_cast<float>(useRootCoords ? root_y : win_y);

            io.MousePos     = ImVec2(mx * scale, my * scale);
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
                            auto* raw = static_cast<XIRawEvent*>(ev.xcookie.data);
                            if (raw->detail == 4)
                                io.MouseWheel += 1.0f;
                            else if (raw->detail == 5)
                                io.MouseWheel -= 1.0f;
                        }
                        XFreeEventData(g_scrollDisplay, &ev.xcookie);
                    }
                }
            }
        }
    }
} // namespace vkBasalt
