#pragma once

namespace vkBasalt {
    // Overlay window layout
    static constexpr float kDebounceDelay       = 0.25f;   // seconds before live preview triggers
    static constexpr float kSnapThreshold       = 150.0f;  // pixels from screen edge to trigger snap
    static constexpr float kMinWindowWidth      = 600.0f;  // minimum overlay window width
    static constexpr float kDefaultWindowWidth  = 1080.0f; // default overlay window width
    static constexpr float kChainPanelWidth     = 260.0f;  // left panel width in shaders tab

    // Widget dimensions
    static constexpr float kSliderWidth         = 250.0f;  // slider/combo width in calibration panels
    static constexpr float kIndentWidth         = 16.0f;   // indentation for nested controls
    static constexpr float kCategoryIndent      = 8.0f;    // indentation for category items
    static constexpr float kFileBrowserHeight   = 200.0f;  // file browser child window height
    static constexpr float kScopeMaxSize        = 512.0f;  // max scope image display size
    static constexpr float kScopeMinSize        = 100.0f;  // min scope image display size
    static constexpr float kScopeFallbackSize   = 256.0f;  // fallback scope size when window is tiny

    // Shader / font
    static constexpr float kBaseFontSize        = 13.0f;   // default ImGui font size in pixels
    static constexpr float kDefaultFontScale    = 1.1f;    // default font scale multiplier

    // Drag / step behavior
    static constexpr float kDragSpeedDivisor    = 200.0f;  // range / divisor = drag speed

    // Scale detection
    static constexpr float kMinScale            = 0.5f;    // minimum valid UI scale
    static constexpr float kFontScaleThreshold  = 1.01f;   // apply custom font above this effective size
} // namespace vkBasalt
