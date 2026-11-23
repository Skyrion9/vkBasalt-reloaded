#pragma once

#include "vulkan_include.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    struct LogicalDevice;

    struct TimingSlot
    {
        std::string name;
        float gpuMs     = 0.0f;
        float cpuMs     = 0.0f;
        float gpuPeakMs = 0.0f;
    };

    // Command buffers are recorded once with SIMULTANEOUS_USE_BIT, so timestamp queries must be
    // baked in at record time and only read back per frame. Each swapchain image gets its own
    // query range so submissions of different images cannot clobber each other's results.
    //
    // A BOTTOM_OF_PIPE timestamp records when prior work finished. It's not an execution barrier.
    // Adjacent slots can overlap on the GPU, so the sum of slots may exceed the measured total.
    // These numbers are an attribution, not a strict partition.
    struct ChainTiming
    {
        VkQueryPool pool         = VK_NULL_HANDLE;
        uint32_t queriesPerImage = 0;
        uint32_t imageCount      = 0;
        std::vector<TimingSlot> slots;
        float totalGpuMs = 0.0f;
        float totalCpuMs = 0.0f;
        float buildMs    = 0.0f;

        bool active() const { return pool != VK_NULL_HANDLE && queriesPerImage > 0; }

        void ensure(LogicalDevice* dev, const std::vector<std::string>& names, uint32_t images);
        void destroy(LogicalDevice* dev);
        void recordBegin(LogicalDevice* dev, VkCommandBuffer cmd, uint32_t imageIndex) const;
        void recordSlot(LogicalDevice* dev, VkCommandBuffer cmd, uint32_t imageIndex, uint32_t slot, bool begin) const;
        void read(LogicalDevice* dev, uint32_t imageIndex);
        void addCpuSample(uint32_t slot, float ms);
    };

    // Returns the chain to record into, or nullptr when timing is off. Releases the pool in the
    // disabled case so toggling the config key does not leak a query pool until swapchain teardown.
    ChainTiming* resolveTiming(LogicalDevice* dev, ChainTiming& chain, bool enabled);
} // namespace vkBasalt
