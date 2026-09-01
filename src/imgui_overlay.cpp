#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "keyboard_input_x11.hpp"
#include "effect.hpp"
#include "imgui_overlay.hpp"
#include "imgui_theme.hpp"
#include "overlay_manager.hpp"

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include "keyboard_input_wayland.hpp"
#endif

#include <vulkan/vulkan_core.h>
#include <unistd.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"

namespace vkBasalt {

    VkDescriptorPool ImGuiOverlay::s_descriptorPool = VK_NULL_HANDLE;
    int ImGuiOverlay::s_instanceCount = 0;

    std::string ImGuiOverlay::doubleToConfigString(double val) {
        std::string s = std::to_string(val);
        std::replace(s.begin(), s.end(), ',', '.');
        return s;
    }

    ImGuiOverlay::ImGuiOverlay(LogicalDevice* pDevice, LogicalSwapchain* pSwapchain, Config* pConfig)
        : m_pDevice(pDevice), m_pSwapchain(pSwapchain), m_pConfig(pConfig) {
        s_instanceCount++;
    }

    double ImGuiOverlay::getUIParam(const std::string& key, Effect* effect) {
        auto it = m_uiParamCache.find(key);
        if (it != m_uiParamCache.end()) return it->second;
        double val = effect ? effect->getParam(key) : 0.0;
        m_uiParamCache[key] = val;
        return val;
    }

    void ImGuiOverlay::setUIParam(const std::string& key, double val) {
        m_uiParamCache[key] = val;
    }

    void ImGuiOverlay::destroyRenderResources() {
        for (auto fb : m_framebuffers) { if (fb) m_pDevice->vkd.DestroyFramebuffer(m_pDevice->device, fb, nullptr); }
        for (auto iv : m_imageViews)   { if (iv) m_pDevice->vkd.DestroyImageView(m_pDevice->device, iv, nullptr); }
        if (m_renderPass) { m_pDevice->vkd.DestroyRenderPass(m_pDevice->device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
        m_framebuffers.clear();
        m_imageViews.clear();
        m_scopeTexturesRegistered = false;
        m_scopeTextureIDs = {};
    }

    ImGuiOverlay::~ImGuiOverlay() {
        destroyRenderResources();
        s_instanceCount--;
        if (s_instanceCount == 0) {
            if (ImGui::GetCurrentContext()) {
                ImGui_ImplVulkan_Shutdown();
                ImGui::DestroyContext();
            }
            if (s_descriptorPool) {
                m_pDevice->vkd.DestroyDescriptorPool(m_pDevice->device, s_descriptorPool, nullptr);
                s_descriptorPool = VK_NULL_HANDLE;
            }
        }
    }

    void ImGuiOverlay::initImGui(VkFormat format) {
        if (m_isInitialized && m_format == format) {
            return;
        }

        if (m_isInitialized) {
            destroyRenderResources();
            if (ImGui::GetCurrentContext()) { ImGui_ImplVulkan_Shutdown(); }
            m_isInitialized = false;
        }

        m_format = format;
        Logger::debug("initImGui: Creating RenderPass...");
        
        VkAttachmentDescription attachment = {};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; 
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // Must match the layout after our barrier in processFrame
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; 
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        
        m_pDevice->vkd.CreateRenderPass(m_pDevice->device, &info, nullptr, &m_renderPass);
        Logger::debug("initImGui: RenderPass created.");

        m_imageViews.resize(m_pSwapchain->imageCount, VK_NULL_HANDLE);
        m_framebuffers.resize(m_pSwapchain->imageCount, VK_NULL_HANDLE);

        if (!ImGui::GetCurrentContext()) {
            Logger::debug("initImGui: Creating ImGui Context...");
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

                     // Scale detection helper
         auto detectScale = [&]() -> float {
             float s = 1.0f;
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
             if (getenv("WAYLAND_DISPLAY") && isWaylandInputActive()) {
                 s = getWaylandUIScale();
             }
             if (s <= 1.001f) {
                 float x11s = getX11UIScale();
                 if (x11s > s) s = x11s;
             }
#else
             s = getX11UIScale();
#endif
             return (s >= 0.5f) ? s : 1.0f;
         };

            // Cursor Area Scale ONLY affects mouse coordinate transformation
            float cursorScale = m_pConfig->getOption<float>("cursorScale", -1.0f);
            if (cursorScale < 0.0f) cursorScale = m_pConfig->getOption<float>("overlayScale", 0.0f); // backward compat
            if (cursorScale <= 0.0f) cursorScale = detectScale();
            m_cursorScale = cursorScale;
            Logger::debug("cursorScale: " + std::to_string(m_cursorScale));

            // UI Scale affects element sizes, padding, spacing (NOT mouse)
            float uiScale = m_pConfig->getOption<float>("uiScale", 0.0f);
            if (uiScale <= 0.0f) uiScale = detectScale();
            m_uiScale = uiScale;
            Logger::debug("uiScale: " + std::to_string(m_uiScale));

            // Font Scale additional multiplier on top of uiScale for fonts only
            float fontScale = m_pConfig->getOption<float>("fontScale", 0.0f);
            if (fontScale <= 0.0f) fontScale = 1.0f;
            m_fontScale = fontScale;
            Logger::debug("fontScale: " + std::to_string(m_fontScale));

            // Apply UI scale to padding/spacing/widget sizes
            if (m_uiScale > 0.5f) {
                ImGui::GetStyle().ScaleAllSizes(m_uiScale);
            }

            // Apply font scale (uiScale * fontScale)
            float effectiveFont = m_uiScale * m_fontScale;
            if (effectiveFont > 1.01f) {
                ImFontConfig fc;
                fc.SizePixels = (float)(int)(13.0f * effectiveFont + 0.5f);
                io.Fonts->AddFontDefault(&fc);
            }

            applyThemeFromConfig(m_pConfig);
        }
        
        if (!s_descriptorPool) {
            Logger::debug("initImGui: Creating Descriptor Pool...");
            VkDescriptorPoolSize pool_sizes[] = {
                { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
            pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            m_pDevice->vkd.CreateDescriptorPool(m_pDevice->device, &pool_info, nullptr, &s_descriptorPool);
        }
        
        ImGuiIO& io = ImGui::GetIO();
        if (!io.BackendRendererUserData) {
            // Cache memory properties for ImGui to avoid loader PID checks
            ImGui_ImplVulkan_SetMemoryProperties(&m_pDevice->memoryProperties);
            Logger::debug("initImGui: Loading ImGui Vulkan functions via layer dispatch...");
            
            // Force ImGui to use vkBasalt's dispatch tables. If we don't do this, ImGui calls global libvulkan.so functions directly,
            // which crashes because the VkPhysicalDevice handle is wrapped by the layer chain.
            auto loader_func = [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {
                LogicalDevice* dev = static_cast<LogicalDevice*>(user_data);
                if (!dev || dev->device == VK_NULL_HANDLE) return nullptr;
                PFN_vkVoidFunction func = dev->vkd.GetDeviceProcAddr(dev->device, function_name);
                if (!func) {
                    func = dev->vki.GetInstanceProcAddr(dev->instance, function_name);
                }
                return func;
            };
            ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, loader_func, m_pDevice);

            Logger::debug("initImGui: Calling ImGui_ImplVulkan_Init...");
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = m_pDevice->instance;
            init_info.PhysicalDevice = m_pDevice->physicalDevice;
            init_info.Device = m_pDevice->device;
            init_info.QueueFamily = m_pDevice->queueFamilyIndex;
            init_info.Queue = m_pDevice->queue;
            init_info.DescriptorPool = s_descriptorPool;
            
            init_info.PipelineInfoMain.RenderPass = m_renderPass;
            init_info.PipelineInfoMain.Subpass = 0;
            init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            
            init_info.MinImageCount = 2;
            init_info.ImageCount = std::max(2u, m_pSwapchain->imageCount); 
            
            ImGui_ImplVulkan_SetMemoryProperties(&m_pDevice->memoryProperties);
            ImGui_ImplVulkan_Init(&init_info);
            Logger::debug("initImGui: ImGui_ImplVulkan_Init returned successfully!");
        }

        m_isInitialized = true;
        Logger::debug("initImGui: Finished successfully.");
    }

    void ImGuiOverlay::reinitImGui() {
        // Destroy current ImGui state so initImGui() runs fresh with new scale
        destroyRenderResources();

        if (ImGui::GetCurrentContext()) {
            ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext();
        }
        // Reset backend flag so initImGui re-runs the full init path
        m_isInitialized = false;
        initImGui(m_format);
        Logger::debug("reinitImGui: Overlay reinitialized with new settings.");
    }

    void ImGuiOverlay::updateInput(uint32_t width, uint32_t height) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);
        io.DeltaTime = 1.0f / 60.0f;

        bool isWayland = false;
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        if (getenv("WAYLAND_DISPLAY") && isWaylandInputActive()) {
            isWayland = true;
        }
#endif

    if (m_isOpen) {
        if (isWayland) {
            // Event-based: feed queued key events directly to ImGui
            feedWaylandKeyEventsToImGui();
            if (wasSlashTypedWayland()) m_focusSearch = true;
        } else {
            auto checkKey = [&](uint32_t keysym) -> bool {
                return isKeyPressedX11(keysym);
            };

            io.AddKeyEvent(ImGuiKey_Tab,        checkKey(0xFF09));
            io.AddKeyEvent(ImGuiKey_LeftArrow,  checkKey(0xFF51));
            io.AddKeyEvent(ImGuiKey_UpArrow,    checkKey(0xFF52));
            io.AddKeyEvent(ImGuiKey_RightArrow, checkKey(0xFF53));
            io.AddKeyEvent(ImGuiKey_DownArrow,  checkKey(0xFF54));
            io.AddKeyEvent(ImGuiKey_Space,      checkKey(0x0020));
            io.AddKeyEvent(ImGuiKey_Enter,      checkKey(0xFF0D));
            io.AddKeyEvent(ImGuiKey_Escape,     checkKey(0xFF1B));
            io.AddKeyEvent(ImGuiKey_Backspace,  checkKey(0xFF08));
            io.AddKeyEvent(ImGuiKey_Delete,     checkKey(0xFFFF));
            io.AddKeyEvent(ImGuiKey_Home,       checkKey(0xFF50));
            io.AddKeyEvent(ImGuiKey_End,        checkKey(0xFF57));
            io.AddKeyEvent(ImGuiKey_PageUp,     checkKey(0xFF55));
            io.AddKeyEvent(ImGuiKey_PageDown,   checkKey(0xFF56));

            for (int i = 0; i < 26; i++) {
                bool down = checkKey(0x0061 + i);
                io.AddKeyEvent((ImGuiKey)((int)ImGuiKey_A + i), down);
            }

            static bool prevSearch = false;
            bool searchDown = checkKey(0x002F) || checkKey(0x0066);
            if (searchDown && !prevSearch) m_focusSearch = true;
            prevSearch = searchDown;

            bool periodDown = checkKey(0x002E);
            bool minusDown  = checkKey(0x002D);
            io.AddKeyEvent(ImGuiKey_Period, periodDown);
            io.AddKeyEvent(ImGuiKey_Minus,  minusDown);

            static bool prevKeys[12] = {};
            for (int i = 0; i <= 9; i++) {
                bool down = checkKey(0x0030 + i) || checkKey(0xFFB0 + i);
                io.AddKeyEvent((ImGuiKey)((int)ImGuiKey_0 + i), down);
                if (down && !prevKeys[i]) io.AddInputCharacter((unsigned int)('0' + i));
                prevKeys[i] = down;
            }
            if (periodDown && !prevKeys[10]) io.AddInputCharacter('.');
            prevKeys[10] = periodDown;
            if (minusDown && !prevKeys[11]) io.AddInputCharacter('-');
            prevKeys[11] = minusDown;
        }
    }

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    if (isWayland) {
        updateWaylandImGuiIO(m_cursorScale);
        return;
}
#endif
    // X11/XWayland coordinates are already in the swapchains pixel space.
    // Don't multiply by UI scale here thats only needed for native Wayland.
    updateX11ImGuiIO(m_isOpen, 1.0f);
    }

    void ImGuiOverlay::processFrame(VkCommandBuffer cmdBuf, uint32_t imageIndex, VkFormat format, uint32_t width, uint32_t height) {
        if (m_screenshotReopenCounter > 0) {
            m_screenshotReopenCounter--;
            if (m_screenshotReopenCounter == 0) {
                m_isOpen = true;
            }
        }

        if (!m_isOpen || !m_isInitialized) {
            Logger::debug("processFrame SKIPPED: isOpen=" + std::to_string(m_isOpen) + ", isInitialized=" + std::to_string(m_isInitialized));
            return;
        }
        if (imageIndex >= m_pSwapchain->imageCount) return;
        Logger::debug("processFrame EXECUTING. Building ImGui draw data...");

        if (m_imageViews[imageIndex] == VK_NULL_HANDLE) {
            VkImageViewCreateInfo view_info = {};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = m_pSwapchain->images[imageIndex];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            m_pDevice->vkd.CreateImageView(m_pDevice->device, &view_info, nullptr, &m_imageViews[imageIndex]);

            VkFramebufferCreateInfo fb_info = {};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.renderPass = m_renderPass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = &m_imageViews[imageIndex];
            fb_info.width = width;
            fb_info.height = height;
            fb_info.layers = 1;
            m_pDevice->vkd.CreateFramebuffer(m_pDevice->device, &fb_info, nullptr, &m_framebuffers[imageIndex]);
        }

        VkImage swapchainImage = m_pSwapchain->images[imageIndex];

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = swapchainImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        m_pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        updateInput(width, height);
    
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
    
        // Games that hide the system cursor (FPS titles, pointer confinement) leave no visible cursor unless we render one ourselves.
        ImGui::GetIO().MouseDrawCursor = true;
    
        drawUI();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        Logger::debug("ImGui DrawData generated: TotalVtxCount=" + std::to_string(draw_data->TotalVtxCount) + ", TotalIdxCount=" + std::to_string(draw_data->TotalIdxCount));

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = m_renderPass;
        info.framebuffer = m_framebuffers[imageIndex];
        info.renderArea.extent.width = width;
        info.renderArea.extent.height = height;
        
        m_pDevice->vkd.CmdBeginRenderPass(cmdBuf, &info, VK_SUBPASS_CONTENTS_INLINE);
        
        // ImGui v1.92.x will automatically upload the font texture here on the first frame using our patched fence-based VkQueueSubmit.
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
        
        m_pDevice->vkd.CmdEndRenderPass(cmdBuf);

        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        m_pDevice->vkd.CmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void ImGuiOverlay::drawUI() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();

        if (!m_windowStateInitialized) {
            m_windowWidth = m_pConfig->getOption<float>("overlayWidth", 0.0f);
            m_windowSide = m_pConfig->getOption<std::string>("overlaySide", "left");
            m_windowStateInitialized = true;
        }

        // Full height, user-resizable width, draggable with edge snapping
        float initWidth = (m_windowWidth > 300.0f) ? m_windowWidth : 1080.0f;
        ImGui::SetNextWindowSize(ImVec2(initWidth, io.DisplaySize.y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, io.DisplaySize.y), ImVec2(FLT_MAX, io.DisplaySize.y));

        // Initial position based on saved side (only on first use)
        float initX = (m_windowSide == "right") ? (io.DisplaySize.x - initWidth) : 0.0f;
        ImGui::SetNextWindowPos(ImVec2(initX, 0), ImGuiCond_FirstUseEver);

        ImGui::Begin("vkBasalt-reloaded Configuration", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Persist width on resize
        float currentWidth = ImGui::GetWindowWidth();

        if (m_lastWidth > 0.0f && std::fabs(currentWidth - m_lastWidth) > 1.0f) {
            m_windowWidth = currentWidth;
            m_pConfig->setGlobalOption("overlayWidth", doubleToConfigString(currentWidth));
            m_pConfig->saveGlobal();
        }
        m_lastWidth = currentWidth;

        // Edge Snapping for ImGui window placement
        ImVec2 currentPos = ImGui::GetWindowPos();
        float windowWidth = ImGui::GetWindowWidth();
        
        bool posChanged = (std::fabs(currentPos.x - m_lastWindowPos.x) > 0.5f || 
                           std::fabs(currentPos.y - m_lastWindowPos.y) > 0.5f);
        bool mouseDown = ImGui::IsMouseDown(0);

        // State machine for dragging
        if (mouseDown) {
            if (posChanged) m_wasWindowMoving = true;
        } else {
            // Mouse released handle snap if we were dragging
            if (m_wasWindowMoving) {
                float snapThreshold = 150.0f;
                bool inLeftZone = currentPos.x < snapThreshold;
                bool inRightZone = (currentPos.x + windowWidth) > (io.DisplaySize.x - snapThreshold);
                
                if (inLeftZone) {
                    ImGui::SetWindowPos(ImVec2(0, 0));
                    m_windowSide = "left";
                    m_pConfig->setGlobalOption("overlaySide", "left");
                    m_pConfig->saveGlobal();
                } else if (inRightZone) {
                    ImGui::SetWindowPos(ImVec2(io.DisplaySize.x - windowWidth, 0));
                    m_windowSide = "right";
                    m_pConfig->setGlobalOption("overlaySide", "right");
                    m_pConfig->saveGlobal();
                }
            }
            m_wasWindowMoving = false;
        }

        // Draw snap indicators
        float snapThreshold = 150.0f;
        bool inLeftZone = currentPos.x < snapThreshold;
        bool inRightZone = (currentPos.x + windowWidth) > (io.DisplaySize.x - snapThreshold);

        if (m_wasWindowMoving && (inLeftZone || inRightZone)) {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            ImVec4 snapColor(0.3f, 0.6f, 1.0f, 0.15f);
            ImVec4 borderColor(0.3f, 0.6f, 1.0f, 0.6f);
            if (inLeftZone) {
                drawList->AddRectFilled(ImVec2(0, 0), ImVec2(windowWidth, io.DisplaySize.y), ImGui::ColorConvertFloat4ToU32(snapColor));
                drawList->AddRect(ImVec2(0, 0), ImVec2(windowWidth, io.DisplaySize.y), ImGui::ColorConvertFloat4ToU32(borderColor), 0.0f, 0, 2.0f);
            } else {
                float rightX = io.DisplaySize.x - windowWidth;
                drawList->AddRectFilled(ImVec2(rightX, 0), ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGui::ColorConvertFloat4ToU32(snapColor));
                drawList->AddRect(ImVec2(rightX, 0), ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGui::ColorConvertFloat4ToU32(borderColor), 0.0f, 0, 2.0f);
            }
        }

        m_lastWindowPos = currentPos;

        // Shift + Left/Right to cycle tabs pendingTab is only set for 1 frame SetSelected shouldn't persist
        int pendingTab = -1;

        if (m_forceSelectTab) {
            pendingTab = m_activeTab;
            m_forceSelectTab = false;
        }
        
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  pendingTab = (m_activeTab <= 0) ? 4 : m_activeTab - 1;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) pendingTab = (m_activeTab >= 4) ? 0 : m_activeTab + 1;
        }

        // Footer height = separator + single button row + 2 legend lines.
        float footerHeight = 1.0f                                      // separator line
            + style.ItemSpacing.y * 2                                  // spacing around separator
            + ImGui::GetFrameHeightWithSpacing()                       // button row
            + (ImGui::GetTextLineHeightWithSpacing() * 2)              // 2 legend lines
            + style.ItemSpacing.y;                                     // bottom padding

        ImGui::BeginChild("##content_area", ImVec2(0, -footerHeight), false);

        if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_None)) {
            ImVec4 tabTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, tabTextColor);

            if (ImGui::BeginTabItem("Shaders", nullptr, (pendingTab == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_activeTab = 0;
                drawShadersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings", nullptr, (pendingTab == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_activeTab = 1;
                drawSettingsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Presets", nullptr, (pendingTab == 2) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_activeTab = 2;
                drawPresetsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Style", nullptr, (pendingTab == 3) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_activeTab = 3;
                drawStyleTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Stats", nullptr, (pendingTab == 4) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_activeTab = 4;
                drawStatsTab();
                ImGui::EndTabItem();
            }
            ImGui::PopStyleColor();
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        // Footer
        ImGui::Separator();

        if (m_activeTab == 0) { // Shaders tab
            if (ImGui::Button("Save & Apply")) {
                // Resolve selected effect by name, not by index into the active chain
                std::vector<std::string> effectNames = m_pConfig->getOption<std::vector<std::string>>("effects", {"cas"});
                if (m_pSwapchain && m_selectedEffectIndex < effectNames.size()) {
                    std::string selectedName = effectNames[m_selectedEffectIndex];
                    for (auto& eff : m_pSwapchain->effects) {
                        if (eff->getName() == selectedName) {
                            const auto& params = eff->getParamDescs();
                            for (const auto& p : params) {
                                if (p.type == ParamType::Combo) continue;
                                double val = eff->getParam(p.key);
                                m_pConfig->setOption(p.key, doubleToConfigString(val));
                            }
                            break;
                        }
                    }
                }
                m_pConfig->savePerGame();
                m_hasUnsavedChanges = false;
                m_previewDirty = false;
                m_showCloseWarning = false;
                g_triggerSoftReload = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_hasUnsavedChanges);
            if (ImGui::Button("Revert Changes")) {
                m_uiParamCache.clear();
                m_hasUnsavedChanges = false;
                m_previewDirty = false;
                m_showCloseWarning = false;
                g_triggerRevertReload = true;
            }
            ImGui::EndDisabled();
            if (m_hasUnsavedChanges) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved)");
            }
        } else if (m_activeTab == 1) { // Settings tab
            if (ImGui::Button("Save Settings & Apply")) {
                m_pConfig->savePerGame();
                m_isOpen = false;
                g_triggerHotReload = true;
            }
        }

        // Close button right aligned on the same row
        float closeWidth = ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - closeWidth);
        if (ImGui::Button("Close")) {
            if (m_hasUnsavedChanges && !m_showCloseWarning) {
                m_showCloseWarning = true;
            } else {
                m_isOpen = false;
                disableScopesOnClose();
            }
        }
        if (m_showCloseWarning && m_hasUnsavedChanges) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
                "Remember to save your config if you want to keep these changes! Click Close again to dismiss.");
        }

        // Legend (dynamic keybinds)
        std::string kbToggle  = m_pConfig->getOption<std::string>("toggleKey", "Insert");
        std::string kbReload  = m_pConfig->getOption<std::string>("reloadConfigKey", "End");
        std::string kbOverlay = m_pConfig->getOption<std::string>("overlayToggleKey", "Home");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
        std::string kbScreenshot = m_pConfig->getOption<std::string>("screenshotKey", "Delete");
        // Show global effects state in legend
        if (!g_effectsEnabled) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "Effects are BYPASSED (press %s to re-enable)", kbToggle.c_str());
        }
        ImGui::Text("[Tab/Arrows] Navigate    [Enter] Edit    [Left/Right] Adjust    [Shift+Left/Right] Switch tab    [/] Search");
        ImGui::Text("[Space] Toggle checkbox    [Esc] Close    [%s] Toggle    [%s] Reload    [%s] Overlay    [%s] Screenshot",
                    kbToggle.c_str(), kbReload.c_str(), kbOverlay.c_str(), kbScreenshot.c_str());
        ImGui::PopStyleColor();

        // Live Preview Debounce applies to all tabs
        if (m_previewDirty && m_hasUnsavedChanges) {
            float elapsed = ImGui::GetTime() - m_lastChangeTime;
            if (elapsed > 0.25f) {
                g_triggerPreviewReload = true;
                m_previewDirty = false;
            }
        }

        ImGui::End();
    }

    void ImGuiOverlay::setScopesEnabled(bool enabled) {
        if (!m_pSwapchain) return;
        for (auto& pass : m_pSwapchain->computePasses) {
            if (pass->getName() == "frame_analyzer" && pass->isEnabled() != enabled) {
                pass->setEnabled(enabled);
                g_triggerSoftReload = true;
                break;
            }
        }
    }

    void ImGuiOverlay::disableScopesOnClose() {
        setScopesEnabled(false);
        m_lastScopeTab = -1;
        m_scopeTexturesRegistered = false;
        m_lastAnalyzerPtr = nullptr;
    }

    void ImGuiOverlay::toggleOverlay() {
        m_isOpen = !m_isOpen;
        if (!m_isOpen) {
            disableScopesOnClose();
        }
        if (m_isOpen) {
            m_justOpened = true;
    #ifdef VK_USE_PLATFORM_WAYLAND_KHR
            if (getenv("WAYLAND_DISPLAY") && isWaylandInputActive()) {
                clearWaylandInputQueues();
            }
    #endif
        }
        // MouseDrawCursor is enforced per-frame in processFrame(). When closing, explicitly disable so the game regains normal cursor behavior.
        if (!m_isOpen && ImGui::GetCurrentContext()) {
            ImGui::GetIO().MouseDrawCursor = false;
        }
    }

} // namespace vkBasalt
