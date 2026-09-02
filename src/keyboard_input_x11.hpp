#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    uint32_t convertToKeySymX11(std::string key);
    bool     isKeyPressedX11(uint32_t ks);
    float getX11UIScale();

    // Overlay input support
    void initX11Input(void* display_ptr, void* window_ptr);
    void supplementX11MouseButtons();
    void updateX11ImGuiIO(bool overlayOpen, float scale);
} // namespace vkBasalt
