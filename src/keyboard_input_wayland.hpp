#pragma once
#include <string>
#include <cstdint>

namespace vkBasalt
{
    void initWaylandInput(void* display_ptr, void* surface_ptr = nullptr);
    void ensureWaylandRegistryBound();
    void updateWaylandImGuiIO(float scale);
    
    // Event based input for Wayland (avoids layout/keysym issues)
    void feedWaylandKeyEventsToImGui();
    bool wasSlashTypedWayland();
    
    float getWaylandUIScale();
    bool  isWaylandInputActive();
    void shutdownWaylandInput();
    uint32_t convertToKeySymWayland(std::string key);
    bool isKeyPressedWayland(uint32_t ks);
} // namespace vkBasalt
