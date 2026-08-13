#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    uint32_t convertToKeySym(const std::string& key);
    bool     isKeyPressed(uint32_t ks);
    void     setInputBackend(bool wayland);
} // namespace vkBasalt
