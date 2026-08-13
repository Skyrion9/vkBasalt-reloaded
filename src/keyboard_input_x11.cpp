#include "keyboard_input_x11.hpp"
#include "logger.hpp"
#include <X11/X.h>
#include <cstdint>
#include <imgui.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>

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
        Display* dpy = g_gameDisplay;
        bool own = false;
        if (!dpy) { dpy = XOpenDisplay(getenv("DISPLAY")); own = true; }
        float scale = 1.0f;
        if (dpy) {
            char* dpi = XGetDefault(dpy, "Xft", "dpi");
            if (dpi) { float d = (float)std::atof(dpi); if (d > 0.0f) scale = d / 96.0f; }
            if (own) XCloseDisplay(dpy);
        }
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
