#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    uint32_t convertToKeySymX11(const std::string& key);
    bool isKeyPressedX11(uint32_t ks);
    float getX11UIScale();

    // Overlay input support
    void initX11Input(void* display_ptr, void* window_ptr);

    // Per frame override for the window the overlay renders into. nullptr clears it.
    void setX11InputTarget(void* display_ptr, void* window_ptr);

    // True when an X server is reachable at all (game connection or DISPLAY).
    bool isX11PointerAvailable();
    void supplementX11MouseButtons();
    void updateX11ImGuiIO(bool overlayOpen, float scale);
} // namespace vkBasalt
