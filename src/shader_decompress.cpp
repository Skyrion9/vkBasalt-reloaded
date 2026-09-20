#include "shader_decompress.hpp"
#include "logger.hpp"
#include "shader_dict.h"
#include <zstd.h>
#include <string>
#include <mutex>
#include <unordered_map>

namespace vkBasalt
{

    static ZSTD_DDict* g_shaderDDict = nullptr;
    static std::once_flag g_dictInitFlag;
    static std::unordered_map<const uint8_t*, std::vector<uint32_t>> g_shaderCache;
    static std::mutex g_shaderCacheMutex;

    static void initDictionary()
    {
        g_shaderDDict = ZSTD_createDDict(vkbasalt_shader_dict, vkbasalt_shader_dict_size);
        if (!g_shaderDDict) {
            Logger::err("Failed to create ZSTD dictionary");
        }
    }

    std::vector<uint32_t> decompressShader(const CompressedShader& shader)
    {
        std::call_once(g_dictInitFlag, initDictionary);

        size_t alignedSize = (shader.originalSize + 3) & ~static_cast<size_t>(3);
        std::vector<uint32_t> spirv(alignedSize / sizeof(uint32_t));

        size_t result = 0;
        if (g_shaderDDict) {
            // Temporary context to use the dictionary.
            ZSTD_DCtx* dctx = ZSTD_createDCtx();
            result          = ZSTD_decompress_usingDDict(
                dctx, spirv.data(), alignedSize, shader.data, shader.compressedSize, g_shaderDDict);
            ZSTD_freeDCtx(dctx);
        } else {
            result = ZSTD_decompress(spirv.data(), alignedSize, shader.data, shader.compressedSize);
        }

        if (ZSTD_isError(result)) {
            Logger::err(std::string("zstd decompression failed: ") + ZSTD_getErrorName(result));
            return {};
        }
        if (result != shader.originalSize) {
            Logger::err(
                "zstd decompression size mismatch: expected " + std::to_string(shader.originalSize) + ", got "
                + std::to_string(result));
            return {};
        }
        return spirv;
    }

    const std::vector<uint32_t>& decompressShaderCached(const CompressedShader& shader)
    {
        std::scoped_lock lock(g_shaderCacheMutex);

        auto it = g_shaderCache.find(shader.data);
        if (it != g_shaderCache.end()) {
            return it->second;
        }

        auto spirv = decompressShader(shader);
        if (spirv.empty()) {
            auto [insertIt, _] = g_shaderCache.emplace(shader.data, std::vector<uint32_t>{});
            return insertIt->second;
        }

        auto [insertIt, _] = g_shaderCache.emplace(shader.data, std::move(spirv));
        return insertIt->second;
    }

    std::vector<uint8_t> decompressData(const uint8_t* data, size_t compressedSize, size_t originalSize)
    {
        std::vector<uint8_t> out(originalSize);
        size_t result = ZSTD_decompress(out.data(), originalSize, data, compressedSize);
        if (ZSTD_isError(result)) {
            Logger::err(std::string("zstd data decompression failed: ") + ZSTD_getErrorName(result));
            return {};
        }
        if (result != originalSize) {
            Logger::err("zstd data decompression size mismatch");
            return {};
        }
        return out;
    }

} // namespace vkBasalt
