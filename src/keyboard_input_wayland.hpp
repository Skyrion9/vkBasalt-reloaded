#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    void initWaylandInput(void* wl_display_ptr);
    uint32_t convertToKeySymWayland(std::string key);
    bool isKeyPressedWayland(uint32_t ks);
} // namespace vkBasalt
