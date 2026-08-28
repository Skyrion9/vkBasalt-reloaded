#pragma once
#include "vulkan_include.hpp"
#include <cstdint>

namespace vkBasalt
{
    struct LogicalDevice;

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
        virtual bool isEnabled() const { return m_enabled; }

        // Name for UI / config lookup.
        virtual std::string getName() const = 0;

    protected:
        bool m_enabled = false;
    };
} // namespace vkBasalt
