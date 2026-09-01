#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    uint32_t convertToKeySym(const std::string& key);
    bool     isKeyPressed(uint32_t ks);
    bool     isWaylandBackend();
    float    getScaleFromEnvAndKDE();
    void     setInputBackend(bool wayland);
} // namespace vkBasalt
