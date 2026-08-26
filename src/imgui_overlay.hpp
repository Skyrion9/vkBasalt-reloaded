#pragma once

#include "vulkan_include.hpp"
#include "effect.hpp"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <vector>
#include <algorithm>
#include <atomic>
#include <string>
#include <unordered_map>

namespace vkBasalt {
    struct LogicalDevice;
    struct LogicalSwapchain;
    class Config;

    class ImGuiOverlay {
    public:
        ImGuiOverlay(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain, Config* pConfig);
        ~ImGuiOverlay();

        void processFrame(VkCommandBuffer cmdBuf, uint32_t imageIndex, VkFormat format, uint32_t width, uint32_t height);
        void toggleOverlay();
        void initImGui(VkFormat format);
        void reinitImGui();
        void updateConfig(Config* pConfig) { m_pConfig = pConfig; }
        void clearUnsavedChanges() { m_hasUnsavedChanges = false; m_previewDirty = false; m_showCloseWarning = false; }
    
        bool isOverlayOpen() const { return m_isOpen; }
        bool isBindingKeys() const { return m_bindingField >= 0; }

    private:
        void updateInput(uint32_t width, uint32_t height);
        void drawUI();
        void drawShadersTab();
        void drawSettingsTab();
        void drawPresetsTab();
        void drawStyleTab();
        void drawStatsTab();
        void drawChainPanel();
        void drawEffectParamsPanel();
        void drawParamWidget(const EffectParamDesc* p, Effect* selectedEffect);
        void applyKeybind(int field, ImGuiKey key);
        void destroyRenderResources();
        void resetParamToDefault(Effect* effect, const EffectParamDesc& p);
        void setParamDebounced(const std::string& key, const std::string& value);
        void setParamImmediate(const std::string& key, const std::string& value);
    
        double getUIParam(const std::string& key, Effect* effect);
        void setUIParam(const std::string& key, double val);

        LogicalDevice* m_pDevice;
        LogicalSwapchain* m_pSwapchain;
        Config* m_pConfig;

        static std::string doubleToConfigString(double val);
        std::atomic<bool> m_isOpen{false};
        bool m_isInitialized = false;
        bool m_justOpened = false;
        bool m_focusSearch = false;
        float m_cursorScale = 1.0f;
        float m_uiScale     = 1.0f;
        float m_fontScale   = 1.0f;
        float m_lastWidth = 0.0f;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        size_t m_selectedEffectIndex = 0;
        char m_searchFilter[256] = {};
        int m_activeTab = 0;   // 0=Shaders, 1=Settings, 2=Presets
        int m_bindingField = -1; // -1=none, 0=toggle, 1=reload, 2=overlay
        bool        m_showBrowser = false;
        std::string m_browserDir;
        bool        m_showDirBrowser = false;
        std::string m_dirBrowserDir;

        bool m_hasUnsavedChanges = false;
        bool m_previewDirty = false;
        float m_lastChangeTime = 0.0f;
        bool m_chainCacheDirty = true;
        bool m_showCloseWarning = false;
        int  m_screenshotReopenCounter = 0;
        bool m_snapPending = false;
        bool m_wasWindowMoving = false;
        bool m_wasMouseDown = false;
        ImVec2 m_lastWindowPos = {0, 0};

        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_framebuffers;
        std::vector<VkImageView> m_imageViews;
        std::vector<std::string> m_cachedChainList;
        std::vector<std::string> m_cachedAllEffects;

        std::unordered_map<std::string, double> m_uiParamCache;
        float m_windowWidth = 0.0f;
        std::string m_windowSide = "left";
        bool m_windowStateInitialized = false;

        struct BrowserEntry {
            std::string path;
            std::string name;
            bool isDir;
        };
        std::vector<BrowserEntry> m_browserEntries;
        std::string m_browserCachedDir;

        // Static shared resources across all overlays
        static VkDescriptorPool s_descriptorPool;
        static int s_instanceCount;
    };
} // namespace vkBasalt
