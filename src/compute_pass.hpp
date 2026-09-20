#pragma once
#include "vulkan_include.hpp"

#include <cstdint>
#include <string>

namespace vkBasalt
{
    struct LogicalDevice;
    class FrameAnalyzer; // Forward declaration for type safe downcast

    // Created during buildEffectChain (per-swapchain), updatePass() called every frame before recording (per frame data)
    // recordCommands() called inside writeCommandBuffers after effects, Destroyed with the swapchain
    class ComputePass
    {
    public:
        virtual ~ComputePass() = default;

        // Record compute commands into the effect chain command buffer. imageIndex = current swapchain image index.
        virtual void recordCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;

        // Called every frame before command buffer submission (perframe data).
        virtual void updatePass() {}

        virtual void setEnabled(bool enabled) { m_enabled = enabled; }
        [[nodiscard]] virtual bool isEnabled() const { return m_enabled; }

        // Propagate overlay visibility (e.g. to skip GPU work when UI is hidden).
        virtual void setOverlayVisible(bool) {}

        // Type safe downcast for FrameAnalyzer
        virtual FrameAnalyzer* asFrameAnalyzer() { return nullptr; }

        // Name for UI / config lookup.
        [[nodiscard]] virtual std::string getName() const = 0;

    protected:
        bool m_enabled = false;
    };
} // namespace vkBasalt
