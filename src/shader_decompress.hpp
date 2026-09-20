#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace vkBasalt
{

    struct CompressedShader
    {
        const uint8_t* data;
        size_t compressedSize;
        size_t originalSize;
    };

    // Oneshot decompression. Always decompresses, returns a new vector.
    std::vector<uint32_t> decompressShader(const CompressedShader& shader);

    // Cached decompression. Decompresses on first call, returns a const reference to the cached vector on subsequent calls.
    // The reference is valid for the lifetime of the process. Usage: const auto& spirv = decompressShaderCached(crystalclear_frag);
    const std::vector<uint32_t>& decompressShaderCached(const CompressedShader& shader);

    // Generic byte decompressor (used for SMAA lookup textures)
    std::vector<uint8_t> decompressData(const uint8_t* data, size_t compressedSize, size_t originalSize);

} // namespace vkBasalt
