#include "keyboard_input_wayland.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <set>
#include <map>

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
        struct xkb_keymap *keymap_xkb = nullptr;
        struct xkb_state *state_xkb = nullptr;
        std::set<xkb_keysym_t> wl_pressed_keys;

        ~wayland_display()
        {
            wl_pressed_keys.clear();
            if (keyboard) wl_keyboard_destroy(keyboard);
            if (seat) wl_seat_destroy(seat);
            if (queue) wl_event_queue_destroy(queue);
            if (keymap_xkb) xkb_keymap_unref(keymap_xkb);
            if (state_xkb) xkb_state_unref(state_xkb);
        }
    };

    static std::map<struct wl_display *, wayland_display> displays;

    // Forward declarations for listener callback functions
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

    // Define listeners using positional initialization to avoid C++20 designated initializer quirks
    static const struct wl_keyboard_listener keyboard_listener = {
        wl_keyboard_keymap,
        wl_keyboard_enter,
        wl_keyboard_leave,
        wl_keyboard_key,
        wl_keyboard_modifiers,
        wl_keyboard_repeat_info
    };

    static const struct wl_seat_listener seat_listener = {
        seat_handle_capabilities,
        seat_handle_name
    };

    static const struct wl_registry_listener registry_listener = {
        registry_handle_global,
        registry_handle_global_remove
    };

    // Callback implementations
    static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size)
    {
        wayland_display *wayland = (wayland_display *)data;
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }

        char* map_shm = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map_shm == MAP_FAILED) {
            close(fd);
            return;
        }

        if (!context_xkb)
            context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        if (wayland->keymap_xkb && wayland->state_xkb)
        {
            xkb_keymap_unref(wayland->keymap_xkb);
            xkb_state_unref(wayland->state_xkb);
            wayland->keymap_xkb = nullptr;
            wayland->state_xkb = nullptr;
        }

        wayland->keymap_xkb = xkb_keymap_new_from_string(
                context_xkb, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
                XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (wayland->keymap_xkb)
            wayland->state_xkb = xkb_state_new(wayland->keymap_xkb);

        munmap((void*)map_shm, size);
        close(fd);
    }

    static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
    {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        if (!wayland->state_xkb) return;

        uint32_t *key;
        wl_array_for_each(key, keys)
        {
            xkb_keycode_t keycode = *key + 8;
            xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);
            wayland->wl_pressed_keys.insert(keysym);
        }
    }

    static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface)
    {
        wayland_display *wayland = (wayland_display *)data;
        if (wayland) wayland->wl_pressed_keys.clear();
    }

    static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
    {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        if (!wayland->state_xkb) return;

        xkb_keycode_t keycode = key + 8;
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);

        if (state) wayland->wl_pressed_keys.insert(keysym);
        else wayland->wl_pressed_keys.erase(keysym);
    }

    static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                                      uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
    {
        wayland_display *wayland = (wayland_display *)data;
        if (wayland && wayland->state_xkb)
            xkb_state_update_mask(wayland->state_xkb, depressed, latched, locked, 0, 0, group);
    }

    static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {}

    static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
    {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;

        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wayland->keyboard)
        {
            wayland->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(wayland->keyboard, &keyboard_listener, data);
        }
    }
    
    static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {}

    static void registry_handle_global(void *data, struct wl_registry* registry, uint32_t name, const char *interface, uint32_t version)
    {
        if (!data) return;
        wayland_display *wayland = (wayland_display *)data;
        if (strcmp(interface, wl_seat_interface.name) == 0 && !wayland->seat)
        {
            wayland->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
            wl_seat_add_listener(wayland->seat, &seat_listener, data);
        }
    }

    static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

    void initWaylandInput(void* display_ptr)
    {
        struct wl_display *display = (struct wl_display *)display_ptr;
        if (!display || displays.find(display) != displays.end()) return;

        displays[display].ref = 1;
        displays[display].queue = wl_display_create_queue(display);
        
        struct wl_display *display_wrapped = (struct wl_display*)wl_proxy_create_wrapper(display);
        wl_proxy_set_queue((struct wl_proxy*)display_wrapped, displays[display].queue);
        
        struct wl_registry *registry = wl_display_get_registry(display_wrapped);
        wl_proxy_wrapper_destroy(display_wrapped);
        
        wl_registry_add_listener(registry, &registry_listener, &displays[display]);
        
        // Roundtrip to get the seat and keyboard
        wl_display_roundtrip_queue(display, displays[display].queue);
        wl_display_roundtrip_queue(display, displays[display].queue);
        
        wl_registry_destroy(registry);
        Logger::debug("Wayland input: Piggybacked on game's wl_display successfully.");
    }

    uint32_t convertToKeySymWayland(std::string key)
    {
        if (!context_xkb) context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context_xkb) return 0;

        xkb_keysym_t keysym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (keysym != XKB_KEY_NoSymbol) return (uint32_t)keysym;

        keysym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        if (keysym != XKB_KEY_NoSymbol) return (uint32_t)keysym;

        if (key == "Escape") return XKB_KEY_Escape;
        if (key == "Return" || key == "Enter") return XKB_KEY_Return;
        if (key == "Space") return XKB_KEY_space;
        if (key == "Tab") return XKB_KEY_Tab;
        if (key == "BackSpace") return XKB_KEY_BackSpace;

        return 0;
    }

    bool isKeyPressedWayland(uint32_t ks)
    {
        bool pressed = false;
        for (const auto& display : displays)
        {
            // Dispatch pending events on our dedicated queue so we don't block the game
            wl_display_dispatch_queue_pending(display.first, display.second.queue);
            
            if (display.second.wl_pressed_keys.count((xkb_keysym_t)ks))
            {
                pressed = true;
                break;
            }
        }
        return pressed;
    }

} // namespace vkBasalt
