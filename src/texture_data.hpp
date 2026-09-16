#pragma once
#include <cstdint>
#include <cstddef>

namespace vkBasalt {
    // Area Texture
    extern const uint8_t areaTex_zst[];
    extern const size_t areaTex_zst_size;
    extern const size_t areaTex_size;
    
    // Search Texture
    extern const uint8_t searchTex_zst[];
    extern const size_t searchTex_zst_size;
    extern const size_t searchTex_size;
    
    // Dimensions
    constexpr uint32_t areaTex_WIDTH = 160;
    constexpr uint32_t areaTex_HEIGHT = 560;
    constexpr uint32_t searchTex_WIDTH = 64;
    constexpr uint32_t searchTex_HEIGHT = 16;
}
