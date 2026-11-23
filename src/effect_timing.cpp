#include "effect_timing.hpp"
#include "logical_device.hpp"
#include "logger.hpp"
#include <algorithm>

namespace vkBasalt
{
    namespace
    {
        constexpr float kEmaWeight = 0.1f;
        constexpr float kPeakDecay = 0.98f;

        float ema(float prev, float sample)
        {
            return prev <= 0.0f ? sample : prev * (1.0f - kEmaWeight) + sample * kEmaWeight;
        }
    } // namespace

    ChainTiming* resolveTiming(LogicalDevice* dev, ChainTiming& chain, bool enabled)
    {
        if (!dev || !enabled || dev->timestampValidBits == 0) {
            chain.destroy(dev);
            return nullptr;
        }
        return &chain;
    }

    void ChainTiming::ensure(LogicalDevice* dev, const std::vector<std::string>& names, uint32_t images)
    {
        if (!dev || dev->timestampValidBits == 0 || names.empty() || images == 0) {
            destroy(dev);
            return;
        }

        const uint32_t wantQueries = static_cast<uint32_t>(names.size()) * 2;

        // Reuse the pool when the layout is unchanged, so soft rebuilds don't churn Vulkan objects.
        if (pool != VK_NULL_HANDLE && queriesPerImage == wantQueries && imageCount == images
            && slots.size() == names.size()) {
            for (size_t i = 0; i < names.size(); i++) {
                if (slots[i].name != names[i]) {
                    slots[i]      = TimingSlot{};
                    slots[i].name = names[i];
                }
            }
            return;
        }

        destroy(dev);

        VkQueryPoolCreateInfo info = {};
        info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount            = wantQueries * images;

        if (dev->vkd.CreateQueryPool(dev->device, &info, nullptr, &pool) != VK_SUCCESS) {
            pool = VK_NULL_HANDLE;
            Logger::warn("Effect timing: CreateQueryPool failed, GPU timing disabled");
            return;
        }

        queriesPerImage = wantQueries;
        imageCount      = images;
        slots.clear();
        slots.reserve(names.size());
        for (const auto& n : names) {
            TimingSlot s;
            s.name = n;
            slots.push_back(s);
        }
    }

    void ChainTiming::destroy(LogicalDevice* dev)
    {
        if (dev && pool != VK_NULL_HANDLE) dev->vkd.DestroyQueryPool(dev->device, pool, nullptr);
        pool            = VK_NULL_HANDLE;
        queriesPerImage = 0;
        imageCount      = 0;
        slots.clear();
        totalGpuMs = 0.0f;
        totalCpuMs = 0.0f;
        buildMs    = 0.0f;
    }

    void ChainTiming::recordBegin(LogicalDevice* dev, VkCommandBuffer cmd, uint32_t imageIndex) const
    {
        if (!active() || imageIndex >= imageCount) return;
        dev->vkd.CmdResetQueryPool(cmd, pool, imageIndex * queriesPerImage, queriesPerImage);
    }

    void ChainTiming::recordSlot(
        LogicalDevice* dev, VkCommandBuffer cmd, uint32_t imageIndex, uint32_t slot, bool begin) const
    {
        if (!active() || imageIndex >= imageCount || slot >= slots.size()) return;
        const uint32_t query = imageIndex * queriesPerImage + slot * 2 + (begin ? 0u : 1u);
        dev->vkd.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, query);
    }

    void ChainTiming::read(LogicalDevice* dev, uint32_t imageIndex)
    {
        if (!active() || imageIndex >= imageCount || dev->timestampPeriod <= 0.0f) return;

        std::vector<uint64_t> raw(queriesPerImage);
        VkResult r = dev->vkd.GetQueryPoolResults(
            dev->device, pool, imageIndex * queriesPerImage, queriesPerImage, raw.size() * sizeof(uint64_t), raw.data(),
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        // VK_NOT_READY means the previous submission for this index is still in flight.
        // Keep the last EMA rather than stalling the present thread.
        if (r != VK_SUCCESS) return;

        const uint32_t bits = dev->timestampValidBits;
        const uint64_t mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);

        for (size_t s = 0; s < slots.size(); s++) {
            const uint64_t start = raw[s * 2] & mask;
            const uint64_t end   = raw[s * 2 + 1] & mask;
            const uint64_t delta = (end - start) & mask;
            const float ms       = static_cast<float>(static_cast<double>(delta) * dev->timestampPeriod) / 1e6f;
            slots[s].gpuMs       = ema(slots[s].gpuMs, ms);
            slots[s].gpuPeakMs   = std::max(ms, slots[s].gpuPeakMs * kPeakDecay);
        }

        const uint64_t first = raw[0] & mask;
        const uint64_t last  = raw[queriesPerImage - 1] & mask;
        totalGpuMs           = ema(
            totalGpuMs, static_cast<float>(static_cast<double>((last - first) & mask) * dev->timestampPeriod) / 1e6f);

        static uint32_t s_logCounter = 0;
        if ((s_logCounter++ % 300) == 0) {
            Logger::debug(
                "Effect timing: GPU " + std::to_string(totalGpuMs) + " ms, CPU " + std::to_string(totalCpuMs)
                + " ms, slots=" + std::to_string(slots.size()));
        }
    }

    void ChainTiming::addCpuSample(uint32_t slot, float ms)
    {
        if (slot >= slots.size()) return;
        slots[slot].cpuMs = ema(slots[slot].cpuMs, ms);
        float sum         = 0.0f;
        for (const auto& s : slots)
            sum += s.cpuMs;
        totalCpuMs = sum;
    }
} // namespace vkBasalt
