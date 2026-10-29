#include "imgui_overlay.hpp"
#include "overlay_manager.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "effect.hpp"
#include "imgui.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace vkBasalt
{

    void ImGuiOverlay::drawPresetsTab()
    {
        ImGui::Text("Preset Management");
        ImGui::Separator();
        ImGui::Spacing();

        // Save New Preset
        static char presetName[256] = {};

        ImGui::PushItemWidth(300);
        ImGui::InputTextWithHint("##preset_name", "New preset name", presetName, sizeof(presetName));
        ImGui::PopItemWidth();
        ImGui::SameLine();

        if (ImGui::Button("Save Current as Preset")) {
            if (presetName[0] != '\0') {
                // Resolve selected effect by name from the unified allEffects list
                if (m_pSwapchain && m_selectedEffectIndex < m_cachedAllEffects.size()) {
                    std::string selectedName = m_cachedAllEffects[m_selectedEffectIndex];
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
                std::string nameStr = presetName;
                m_pConfig->savePreset(nameStr);
                presetName[0]      = '\0';
                m_presetStatusMsg  = "Saved preset: " + nameStr;
                m_presetStatusTime = ImGui::GetTime();
                m_presetBrowserCachedDir.clear();
            }
        }

        ImGui::Spacing();

        // Import from File
        if (ImGui::Button("Import Preset from File...")) {
            m_showPresetBrowser = !m_showPresetBrowser;
            if (m_showPresetBrowser && m_presetBrowserDir.empty()) {
                const char* home   = std::getenv("HOME");
                m_presetBrowserDir = home ? std::string(home) + "/Downloads" : ".";
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Browse .conf files, defaults to ~/Downloads)");

        if (m_showPresetBrowser) {
            // Refresh directory listing if path changed
            if (m_presetBrowserDir != m_presetBrowserCachedDir) {
                m_presetBrowserEntries.clear();
                std::error_code ec;
                if (std::filesystem::exists(m_presetBrowserDir, ec)) {
                    for (auto& entry : std::filesystem::directory_iterator(m_presetBrowserDir, ec)) {
                        BrowserEntry be;
                        be.path  = entry.path().string();
                        be.name  = entry.path().filename().string();
                        be.isDir = entry.is_directory();
                        // Only show directories and .conf files
                        if (be.isDir || (be.name.size() > 5 && be.name.substr(be.name.size() - 5) == ".conf")) {
                            m_presetBrowserEntries.push_back(std::move(be));
                        }
                    }
                }
                m_presetBrowserCachedDir = m_presetBrowserDir;
            }

            ImGui::BeginChild("##preset_browser", ImVec2(0, kFileBrowserHeight), true);
            ImGui::Text("Directory: %s", m_presetBrowserDir.c_str());

            // Sort by directories first, then files alphabetically
            std::ranges::sort(m_presetBrowserEntries, [](const BrowserEntry& a, const BrowserEntry& b) {
                if (a.isDir != b.isDir) return a.isDir > b.isDir;
                return a.name < b.name;
            });

            for (const auto& entry : m_presetBrowserEntries) {
                if (entry.isDir) {
                    if (ImGui::Selectable(("[DIR] " + entry.name).c_str())) {
                        m_presetBrowserDir = entry.path;
                    }
                } else {
                    if (ImGui::Selectable(entry.name.c_str())) {
                        // Copy .conf file to presets directory
                        const char* home       = std::getenv("HOME");
                        std::string presetsDir = std::string(home ? home : ".") + "/.config/vkBasalt-reloaded/presets";
                        std::error_code ec;
                        std::filesystem::create_directories(presetsDir, ec);
                        std::string destPath = presetsDir + "/" + entry.name;

                        // If a preset with this name already exists, append a number
                        if (std::filesystem::exists(destPath, ec)) {
                            std::string baseName = entry.name.substr(0, entry.name.size() - 5);
                            for (int i = 2; i < 100; i++) {
                                destPath = presetsDir + "/" + baseName + "_" + std::to_string(i) + ".conf";
                                if (!std::filesystem::exists(destPath, ec)) break;
                            }
                        }

                        std::filesystem::copy_file(
                            entry.path, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            m_presetStatusMsg = "Imported: " + std::filesystem::path(destPath).filename().string();
                            m_presetBrowserCachedDir.clear();
                        } else {
                            m_presetStatusMsg = "Import failed: " + ec.message();
                        }
                        m_presetStatusTime  = ImGui::GetTime();
                        m_showPresetBrowser = false;
                    }
                }
            }

            // Parent directory button
            if (ImGui::Button(".. (parent)")) {
                std::filesystem::path parent = std::filesystem::path(m_presetBrowserDir).parent_path();
                if (!parent.empty()) m_presetBrowserDir = parent.string();
            }

            // Path input for manual navigation
            ImGui::SameLine();
            static char pathBuf[1024] = {};
            if (m_presetBrowserDir.size() < sizeof(pathBuf) - 1) {
                std::strncpy(pathBuf, m_presetBrowserDir.c_str(), sizeof(pathBuf) - 1);
            }
            ImGui::PushItemWidth(-1.0f);
            if (ImGui::InputText("##preset_path", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::error_code ec;
                if (std::filesystem::is_directory(std::string(pathBuf), ec)) {
                    m_presetBrowserDir = std::string(pathBuf);
                }
            }
            ImGui::PopItemWidth();

            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Status message (auto-dismiss after 5 seconds)
        if (!m_presetStatusMsg.empty()) {
            float elapsed = ImGui::GetTime() - m_presetStatusTime;
            if (elapsed < 5.0f) {
                bool isError = m_presetStatusMsg.find("failed") != std::string::npos
                               || m_presetStatusMsg.find("Failed") != std::string::npos;
                if (isError) {
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", m_presetStatusMsg.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "%s", m_presetStatusMsg.c_str());
                }
            } else {
                m_presetStatusMsg.clear();
            }
        }

        // Saved Presets List
        auto presets = m_pConfig->listPresets();
        if (presets.empty()) {
            ImGui::TextDisabled("No presets saved yet. Save one above or import a .conf file.");
            return;
        }

        ImGui::Text("Saved Presets (%d):", static_cast<int>(presets.size()));
        ImGui::Spacing();

        for (size_t i = 0; i < presets.size(); i++) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s", presets[i].c_str());
            ImGui::SameLine(300);

            if (ImGui::Button("Load")) {
                m_pConfig->loadPreset(presets[i]);
                m_pConfig->savePerGame();
                g_triggerSoftReload = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Export")) {
                const char* home         = std::getenv("HOME");
                std::string downloadsDir = std::string(home ? home : ".") + "/Downloads";
                std::error_code ec;
                std::filesystem::create_directories(downloadsDir, ec);

                std::string srcPath =
                    std::string(home ? home : ".") + "/.config/vkBasalt-reloaded/presets/" + presets[i] + ".conf";
                std::string destPath = downloadsDir + "/" + presets[i] + ".conf";

                // Avoid overwriting existing files in Downloads
                if (std::filesystem::exists(destPath, ec)) {
                    for (int j = 2; j < 100; j++) {
                        destPath = downloadsDir + "/" + presets[i] + "_" + std::to_string(j) + ".conf";
                        if (!std::filesystem::exists(destPath, ec)) break;
                    }
                }

                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    m_presetStatusMsg = "Exported to: " + destPath;
                    m_presetBrowserCachedDir.clear();
                } else {
                    m_presetStatusMsg = "Export failed: " + ec.message();
                }
                m_presetStatusTime = ImGui::GetTime();
            }

            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                m_pConfig->deletePreset(presets[i]);
                m_presetBrowserCachedDir.clear();
            }
            ImGui::PopID();
        }
    }
} // namespace vkBasalt
