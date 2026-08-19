#include "imgui_overlay.hpp"
#include "imgui_theme.hpp"
#include "overlay_manager.hpp"
#include "logical_swapchain.hpp"
#include "config.hpp"
#include "screenshot.hpp"
#include "effect.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace vkBasalt {

    std::string ImGuiOverlay::doubleToConfigString(double val) {
        std::string s = std::to_string(val);
        std::replace(s.begin(), s.end(), ',', '.');
        return s;
    }
} // namespace vkBasalt
