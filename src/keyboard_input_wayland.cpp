#include "keyboard_input_wayland.hpp"
#include "logger.hpp"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include <cfloat>
#include <cstdint>
#include <string>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <cstdlib>

#include <cstring>
#include <set>
#include <map>
#include <vector>
#include <utility>
#include <imgui.h>

#ifdef wl_array_for_each
#undef wl_array_for_each
#endif
#define wl_array_for_each(pos, array)					\
	for (pos = (decltype(pos)) (array)->data;				\
	     (array)->size != 0 &&					\
	     (const char *) pos < ((const char *) (array)->data + (array)->size); \
	     (pos)++)

namespace vkBasalt
{
    struct xkb_context *context_xkb = nullptr;

    struct wayland_display
    {
        int ref = 1;
        struct wl_event_queue *queue = nullptr;
        struct wl_seat *seat = nullptr;
        struct wl_keyboard *keyboard = nullptr;
        struct wl_pointer *pointer = nullptr;
        struct xkb_keymap *keymap_xkb = nullptr;
        struct xkb_state *state_xkb = nullptr;
        std::vector<struct wl_output*> outputs;
        std::set<xkb_keysym_t> wl_pressed_keys;
        
        float mouse_x = 0.0f, mouse_y = 0.0f;
        bool mouse_down[5] = {false, false, false, false, false};
        float mouse_wheel = 0.0f;
        bool mouse_valid = false;

        std::vector<uint32_t> typed_chars;
        std::vector<std::pair<xkb_keysym_t, bool>> key_events; // (keysym, is_pressed)
        struct zwp_relative_pointer_manager_v1* relative_manager = nullptr;
        struct zwp_relative_pointer_v1* relative_pointer = nullptr;

        ~wayland_display()
        {
            wl_pressed_keys.clear();
            if (relative_pointer) zwp_relative_pointer_v1_destroy(relative_pointer);
            if (relative_manager) zwp_relative_pointer_manager_v1_destroy(relative_manager);
            for (auto* out : outputs) {
                if (out) wl_output_destroy(out);
            }
            outputs.clear();
            if (keyboard) wl_keyboard_destroy(keyboard);
            if (pointer) wl_pointer_destroy(pointer);
            if (seat) wl_seat_destroy(seat);
            if (queue) wl_event_queue_destroy(queue);
            if (keymap_xkb) xkb_keymap_unref(keymap_xkb);
            if (state_xkb) xkb_state_unref(state_xkb);
        }
    };

    static std::map<struct wl_display *, wayland_display> displays;

static int   g_outputScale = 1; // Tracks the maximum scale across all displays

    // wl_output listener for integer scale fallback
    static void wl_output_geometry(void*, struct wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
    static void wl_output_mode(void*, struct wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    static void wl_output_done(void*, struct wl_output*) {}
    static void wl_output_scale(void*, struct wl_output*, int32_t factor) {
        if (factor > g_outputScale) g_outputScale = factor;
        Logger::debug("wl_output scale: " + std::to_string(factor) + " (max: " + std::to_string(g_outputScale) + ")");
    }
    static void wl_output_name(void*, struct wl_output*, const char*) {}
    static void wl_output_description(void*, struct wl_output*, const char*) {}
    static const struct wl_output_listener output_listener = {
        wl_output_geometry, wl_output_mode, wl_output_done,
        wl_output_scale, wl_output_name, wl_output_description
    };

    static float detectScaleFromEnv() {
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
        return 0.0f; // not detected
    }

    static float detectScaleFromKDE() {
        const char* home = std::getenv("HOME");
        if (!home) return 0.0f;
        std::string homeStr(home);

        // Try multiple KDE config locations
        std::vector<std::string> paths = {
            homeStr + "/.config/kdeglobals",
            homeStr + "/.config/kwinrc",
        };

        for (const auto& path : paths) {
            std::ifstream f(path);
            if (!f.good()) continue;
            std::string line;
            while (std::getline(f, line)) {
                // KDE Plasma 5
                if (line.rfind("ScreenScaleFactors=", 0) == 0) {
                    std::string val = line.substr(19);
                    size_t comma = val.find(',');
                    if (comma != std::string::npos) val = val.substr(0, comma);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) {
                        Logger::debug("Detected scale from " + path + ": " + std::to_string(s));
                        return s;
                    }
                }
                // KDE Plasma 6
                if (line.rfind("ScaleFactor=", 0) == 0) {
                    std::string val = line.substr(12);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) {
                        Logger::debug("Detected scale from " + path + " (ScaleFactor): " + std::to_string(s));
                        return s;
                    }
                }
                if (line.rfind("Scale=", 0) == 0) {
                    std::string val = line.substr(6);
                    float s = (float)std::atof(val.c_str());
                    if (s >= 0.5f && s <= 5.0f) {
                        Logger::debug("Detected scale from " + path + " (Scale): " + std::to_string(s));
                        return s;
                    }
                }
            }
        }

        // Try xrdb / Xft.dpi via X11 (KDE sets this even under Wayland)
        const char* display = std::getenv("DISPLAY");
        if (display && display[0] != '\0') {
            // We can't call XOpenDisplay here (no X11 headers in this file), but getX11UIScale() in imgui_overlay.cpp handles this.
            Logger::debug("KDE scale not found in config files, will try Xft.dpi");
        }

        return 0.0f;
    }

    float getWaylandUIScale() {
        float envScale = detectScaleFromEnv();
        if (envScale > 0.0f) return envScale;
        float kdeScale = detectScaleFromKDE();
        if (kdeScale > 0.0f) return kdeScale;
        if (g_outputScale > 1) return (float)g_outputScale;
        return 1.0f;
    }
    bool  isWaylandInputActive() { return !displays.empty(); }

    // Relative pointer listener provides mouse deltas during pointer lock
    static void relative_pointer_motion(void *data, struct zwp_relative_pointer_v1 *pointer,
                                        uint32_t time_hi, uint32_t time_lo,
                                        wl_fixed_t dx, wl_fixed_t dy,
                                        wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
        wayland_display *wayland = (wayland_display *)data;
        // Apply deltas to internal position. Clamped in updateWaylandImGuiIO.
        wayland->mouse_x += wl_fixed_to_double(dx_unaccel);
        wayland->mouse_y += wl_fixed_to_double(dy_unaccel);
    }

    static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
        relative_pointer_motion
    };

    static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps);
    static void seat_handle_name(void *data, struct wl_seat *seat, const char *name);
    static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size);
    static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
    static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface);
    static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay);
    static void registry_handle_global(void *data, struct wl_registry* registry, uint32_t name, const char *interface, uint32_t version);
    static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);
    static ImGuiKey keysymToImGuiKey(xkb_keysym_t keysym);

    static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
    static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface);
    static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
    static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value);

    static const struct wl_keyboard_listener keyboard_listener = {
        wl_keyboard_keymap, wl_keyboard_enter, wl_keyboard_leave,
        wl_keyboard_key, wl_keyboard_modifiers, wl_keyboard_repeat_info
    };

    // wl_pointer v5-v9 events we don't need
    static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {}
    static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {}
    static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {}
    static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {}
    static void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {}
    static void wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {}

    static const struct wl_pointer_listener pointer_listener = {
        wl_pointer_enter, wl_pointer_leave, wl_pointer_motion,
        wl_pointer_button, wl_pointer_axis,
        wl_pointer_frame, wl_pointer_axis_source, wl_pointer_axis_stop,
        wl_pointer_axis_discrete, wl_pointer_axis_value120,
        wl_pointer_axis_relative_direction
    };

    static const struct wl_seat_listener seat_listener = {
        seat_handle_capabilities, seat_handle_name
    };

    static const struct wl_registry_listener registry_listener = {
        registry_handle_global, registry_handle_global_remove
    };

    static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
        wayland_display *wayland = (wayland_display *)data;
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
        char* map_shm = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map_shm == MAP_FAILED) { close(fd); return; }
        if (!context_xkb) context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (wayland->keymap_xkb && wayland->state_xkb) {
            xkb_keymap_unref(wayland->keymap_xkb);
            xkb_state_unref(wayland->state_xkb);
        }
        wayland->keymap_xkb = xkb_keymap_new_from_string(context_xkb, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (wayland->keymap_xkb) wayland->state_xkb = xkb_state_new(wayland->keymap_xkb);
        munmap((void*)map_shm, size);
        close(fd);
    }

    static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        if (!wayland->state_xkb) return;
        uint32_t *key;
        wl_array_for_each(key, keys) {
            xkb_keycode_t keycode = *key + 8;
            xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);
            wayland->wl_pressed_keys.insert(keysym);
        }
    }

    static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
        wayland_display *wayland = (wayland_display *)data;
        if (wayland) {
            if (ImGui::GetCurrentContext()) {
                ImGuiIO& io = ImGui::GetIO();
                for (auto keysym : wayland->wl_pressed_keys) {
                    ImGuiKey imguiKey = keysymToImGuiKey(keysym);
                    if (imguiKey != ImGuiKey_None) {
                        io.AddKeyEvent(imguiKey, false);
                    }
                }
            }
            wayland->wl_pressed_keys.clear();
            wayland->key_events.clear();
            wayland->typed_chars.clear();
        }
    }

    static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        if (!wayland->state_xkb) return;
        xkb_keycode_t keycode = key + 8;
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);
        
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            wayland->wl_pressed_keys.insert(keysym);
            wayland->key_events.push_back({keysym, true});
            
            uint32_t codepoint = xkb_state_key_get_utf32(wayland->state_xkb, keycode);
            if (codepoint >= 32 && codepoint != 0x7F) {
                wayland->typed_chars.push_back(codepoint);
            }
        } else {
            wayland->wl_pressed_keys.erase(keysym);
            wayland->key_events.push_back({keysym, false});
        }
    }

    static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
        wayland_display *wayland = (wayland_display *)data;
        if (wayland && wayland->state_xkb) xkb_state_update_mask(wayland->state_xkb, depressed, latched, locked, 0, 0, group);
    }

    static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {}

    static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
        wayland_display *wayland = (wayland_display *)data;
        wayland->mouse_x = wl_fixed_to_double(surface_x);
        wayland->mouse_y = wl_fixed_to_double(surface_y);
        wayland->mouse_valid = true;
    }
    
    static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
        wayland_display *wayland = (wayland_display *)data;
        wayland->mouse_valid = false;
    }
    
    static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
        wayland_display *wayland = (wayland_display *)data;
        wayland->mouse_x = wl_fixed_to_double(surface_x);
        wayland->mouse_y = wl_fixed_to_double(surface_y);
        wayland->mouse_valid = true;
    }
    
    static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
        wayland_display *wayland = (wayland_display *)data;
        int imgui_button = -1;
        if (button == 272) imgui_button = 0;
        else if (button == 273) imgui_button = 1;
        else if (button == 274) imgui_button = 2;
        
        if (imgui_button >= 0 && imgui_button < 5) {
            wayland->mouse_down[imgui_button] = (state == WL_POINTER_BUTTON_STATE_PRESSED);
        }
    }
    
    static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
        wayland_display *wayland = (wayland_display *)data;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            wayland->mouse_wheel += -wl_fixed_to_double(value) / 10.0;
        }
    }

    static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        Logger::debug("seat_handle_capabilities: caps=" + std::to_string(caps));

        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wayland->keyboard) {
            wayland->keyboard = wl_seat_get_keyboard(seat);
            // Route keyboard events to our custom queue, not the game's default queue
            wl_proxy_set_queue((struct wl_proxy*)wayland->keyboard, wayland->queue);
            wl_keyboard_add_listener(wayland->keyboard, &keyboard_listener, data);
            Logger::debug("Bound keyboard to custom queue");
        }
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wayland->pointer) {
            wayland->pointer = wl_seat_get_pointer(seat);
            // Route pointer events to our custom queue
            wl_proxy_set_queue((struct wl_proxy*)wayland->pointer, wayland->queue);
            wl_pointer_add_listener(wayland->pointer, &pointer_listener, data);
            Logger::debug("Bound pointer to custom queue");

            // Create relative pointer for FPS games that lock the cursor
            if (wayland->relative_manager && !wayland->relative_pointer) {
                wayland->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
                    wayland->relative_manager, wayland->pointer);
                // Route relative pointer events to our custom queue
                wl_proxy_set_queue((struct wl_proxy*)wayland->relative_pointer, wayland->queue);
                zwp_relative_pointer_v1_add_listener(wayland->relative_pointer, &relative_pointer_listener, data);
                Logger::debug("Bound relative_pointer to custom queue");
            }
        }
    }

    static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {}

    static void registry_handle_global(void *data, struct wl_registry* registry, uint32_t name, const char *interface, uint32_t version) {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;

        if (strcmp(interface, wl_seat_interface.name) == 0 && !wayland->seat) {
            uint32_t bind_version = (version < 9) ? version : 9;
            struct wl_seat* seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
            wl_proxy_set_queue((struct wl_proxy*)seat, wayland->queue);
            wayland->seat = seat;
            wl_seat_add_listener(wayland->seat, &seat_listener, data);
            Logger::debug("Bound wl_seat to custom queue");
        }
        if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
            wayland->relative_manager = (struct zwp_relative_pointer_manager_v1*)wl_registry_bind(
                registry, name, &zwp_relative_pointer_manager_v1_interface, 1);
            // Route manager events to our custom queue
            wl_proxy_set_queue((struct wl_proxy*)wayland->relative_manager, wayland->queue);
            Logger::debug("Bound relative_pointer_manager");
        }

        if (strcmp(interface, wl_output_interface.name) == 0) {
            struct wl_output* output = (struct wl_output*)wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
            wl_proxy_set_queue((struct wl_proxy*)output, wayland->queue);
            wl_output_add_listener(output, &output_listener, nullptr);
            wayland->outputs.push_back(output);
        }
    }

    static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

    static bool g_registryInitialized = false;
    static struct wl_display* g_pendingDisplay = nullptr;

    void initWaylandInput(void* display_ptr, void* surface_ptr) {
        (void)surface_ptr;
        struct wl_display *display = (struct wl_display *)display_ptr;
        if (!display || displays.find(display) != displays.end()) return;
        
        // Only store the display pointer. Don't create queue or do any Wayland operations here.
        // The game's event loop may not be ready. Everything is deferred to ensureWaylandRegistryBound() which runs
        // on the first QueuePresentKHR when the game is fully initialized.
        displays[display].ref = 1;
        displays[display].queue = nullptr;
        g_pendingDisplay = display;
        g_registryInitialized = false;
        
        Logger::debug("Wayland input: display registered, deferring all Wayland setup.");
    }

    // Called lazily on the first QueuePresentKHR to safely bind registry globals
    void ensureWaylandRegistryBound() {
        if (g_registryInitialized || !g_pendingDisplay) return;
        g_registryInitialized = true;
        
        struct wl_display* display = g_pendingDisplay;
        auto& wayland = displays[display];
        
        // Create the queue here, not in initWaylandInput, to avoid touching the display during the game's early initialization phase.
        if (!wayland.queue) {
            wayland.queue = wl_display_create_queue(display);
        }
        
        struct wl_display *display_wrapped = (struct wl_display*)wl_proxy_create_wrapper(display);
        wl_proxy_set_queue((struct wl_proxy*)display_wrapped, wayland.queue);
        struct wl_registry *registry = wl_display_get_registry(display_wrapped);
        wl_proxy_wrapper_destroy(display_wrapped);
        wl_registry_add_listener(registry, &registry_listener, &wayland);
        wl_display_roundtrip_queue(display, wayland.queue);
        wl_display_roundtrip_queue(display, wayland.queue);
        wl_registry_destroy(registry);
        
        Logger::debug("Wayland registry bound. Output scale: " + std::to_string(g_outputScale));
    }

    uint32_t convertToKeySymWayland(std::string key) {
        if (!context_xkb) context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context_xkb) return 0;
        xkb_keysym_t keysym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (keysym != XKB_KEY_NoSymbol) return (uint32_t)keysym;
        keysym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        if (keysym != XKB_KEY_NoSymbol) return (uint32_t)keysym;
        return 0;
    }

    bool isKeyPressedWayland(uint32_t ks) {
        bool pressed = false;
        for (const auto& display : displays) {
            if (!display.second.queue) continue; // Queue not yet created — skip
            wl_display_dispatch_queue_pending(display.first, display.second.queue);
            if (display.second.wl_pressed_keys.count((xkb_keysym_t)ks)) {
                pressed = true;
                break;
            }
        }
        return pressed;
    }

    static ImGuiKey keysymToImGuiKey(xkb_keysym_t keysym) {
        switch (keysym) {
            // Navigation & editing for ImGui keyboard nav
            case 0xFF09: return ImGuiKey_Tab;
            case 0xFF51: return ImGuiKey_LeftArrow;
            case 0xFF52: return ImGuiKey_UpArrow;
            case 0xFF53: return ImGuiKey_RightArrow;
            case 0xFF54: return ImGuiKey_DownArrow;
            case 0x0020: return ImGuiKey_Space;
            case 0xFF0D: return ImGuiKey_Enter;
            case 0xFF1B: return ImGuiKey_Escape;
            case 0xFF08: return ImGuiKey_Backspace;
            case 0xFFFF: return ImGuiKey_Delete;
            case 0x002D: return ImGuiKey_Minus;
            case 0x002F: return ImGuiKey_Slash;
            case 0xFF80: return ImGuiKey_KeypadEnter;
            case 0xFF50: return ImGuiKey_Home;
            case 0xFF57: return ImGuiKey_End;
            case 0xFF55: return ImGuiKey_PageUp;
            case 0xFF56: return ImGuiKey_PageDown;
            case 0xFF63: return ImGuiKey_Insert;
            case 0xFFE1: return ImGuiKey_LeftShift;
            case 0xFFE2: return ImGuiKey_RightShift;
            case 0xFFE3: return ImGuiKey_LeftCtrl;
            case 0xFFE4: return ImGuiKey_RightCtrl;
            case 0xFFE9: return ImGuiKey_LeftAlt;
            case 0xFFEA: return ImGuiKey_RightAlt;
            case 0xFFE5: return ImGuiKey_CapsLock;
            case 0xFF7F: return ImGuiKey_NumLock;
            case 0xFF14: return ImGuiKey_ScrollLock;
            case 0xFF61: return ImGuiKey_PrintScreen;
            case 0xFF13: return ImGuiKey_Pause;
            case 0xFF8D: return ImGuiKey_KeypadEnter;
            case 0xFFAF: return ImGuiKey_KeypadDivide;
            case 0xFFAA: return ImGuiKey_KeypadMultiply;
            case 0xFFAD: return ImGuiKey_KeypadSubtract;
            case 0xFFAB: return ImGuiKey_KeypadAdd;
            case 0xFFAE: return ImGuiKey_KeypadDecimal;
            case 0x002E: return ImGuiKey_Period;
            default:
                if (keysym >= 0x0030 && keysym <= 0x0039) return (ImGuiKey)((int)ImGuiKey_0 + (keysym - 0x0030));
                if (keysym >= 0xFFB0 && keysym <= 0xFFB9) return (ImGuiKey)((int)ImGuiKey_0 + (keysym - 0xFFB0));
                if (keysym >= 0x0061 && keysym <= 0x007A) return (ImGuiKey)((int)ImGuiKey_A + (keysym - 0x0061));
                if (keysym >= 0x0041 && keysym <= 0x005A) return (ImGuiKey)((int)ImGuiKey_A + (keysym - 0x0041));
                return ImGuiKey_None;
        }
    }

    void feedWaylandKeyEventsToImGui() {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        for (auto& display_pair : displays) {
            wayland_display& wayland = display_pair.second;
            if (!wayland.queue) continue;
            wl_display_dispatch_queue_pending(display_pair.first, wayland.queue);
            
            for (auto& [keysym, pressed] : wayland.key_events) {
                ImGuiKey imguiKey = keysymToImGuiKey(keysym);
                if (imguiKey != ImGuiKey_None) {
                    io.AddKeyEvent(imguiKey, pressed);
                }
            }
            wayland.key_events.clear();
            
            for (uint32_t c : wayland.typed_chars) {
                io.AddInputCharacter(c);
            }
            wayland.typed_chars.clear();
        }
    }

    bool wasSlashTypedWayland() {
        for (auto& display_pair : displays) {
            wayland_display& wayland = display_pair.second;
            if (wayland.wl_pressed_keys.count(0x002F)) return true; // '/' and 'f'
            if (wayland.wl_pressed_keys.count(0x0066)) return true;
        }
        return false;
    }

    void updateWaylandImGuiIO(float scale) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        for (auto& display_pair : displays) {
            wayland_display& wayland = display_pair.second;

            if (wayland.mouse_valid) {
                // Normal absolute position (desktop, windowed mode)
                io.MousePos = ImVec2(wayland.mouse_x * scale, wayland.mouse_y * scale);
            } else {
                // Pointer is locked. Relative motion deltas are applied in the relative_pointer_motion callback. 
                // Clamp to screen bounds so the cursor doesn't drift off screen during gameplay.
                if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0) {
                    float maxX = io.DisplaySize.x / scale;
                    float maxY = io.DisplaySize.y / scale;
                    if (wayland.mouse_x < 0) wayland.mouse_x = 0;
                    if (wayland.mouse_y < 0) wayland.mouse_y = 0;
                    if (wayland.mouse_x > maxX) wayland.mouse_x = maxX;
                    if (wayland.mouse_y > maxY) wayland.mouse_y = maxY;
                }
                io.MousePos = ImVec2(wayland.mouse_x * scale, wayland.mouse_y * scale);
            }

            // Feed buttons unconditionally they still fire during pointer lock
            for (int i = 0; i < 5; i++) io.MouseDown[i] = wayland.mouse_down[i];
            io.MouseWheel += wayland.mouse_wheel;
            wayland.mouse_wheel = 0.0f;
            break;
        }
    }

    void clearWaylandInputQueues() {
        for (auto& display_pair : displays) {
            wayland_display& wayland = display_pair.second;
            wayland.key_events.clear();
            wayland.typed_chars.clear();
            for (int i = 0; i < 5; i++) wayland.mouse_down[i] = false;
            wayland.mouse_wheel = 0.0f;
        }
        if (ImGui::GetCurrentContext()) {
            ImGuiIO& io = ImGui::GetIO();
            io.ClearInputKeys();
            io.InputQueueCharacters.resize(0);
            for (int i = 0; i < 5; i++) io.MouseDown[i] = false;
            io.MouseWheel = 0.0f;
        }
    }

    void shutdownWaylandInput() {
        displays.clear(); // wayland_display destructors clean up all Wayland resources
        if (context_xkb) {
            xkb_context_unref(context_xkb);
            context_xkb = nullptr;
        }
        g_registryInitialized = false;
        g_pendingDisplay = nullptr;
        Logger::debug("Wayland input shut down.");
    }
} // namespace vkBasalt
