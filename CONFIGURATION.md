
# vkBasalt-reloaded Configuration Guide

Complete documentation for configuring shaders, effect chains, and runtime parameters.

## Table of Contents

- [Config File System](#config-file-system)
- [Effect Chains](#effect-chains)
- [Global Configuration](#global-configuration)
- [Shader Parameters](#shader-parameters)
  - [CrystalClear](#crystalclear)
  - [ClarityRCAS](#clarityrcas)
  - [Clarity](#clarity)
  - [SMAA](#smaa)
  - [CAS](#cas)
  - [FXAA](#fxaa)
  - [Deband](#deband)
  - [LUT](#lut)
  - [DLS](#dls)
  - [ReShade Effects](#reshade-effects)
- [Adding New Effects](#adding-new-effects)

---

## Config File System

### File Locations

| File | Path | Purpose |
|------|------|---------|
| Global config | `~/.config/vkBasalt-reloaded/vkBasalt-reloaded.conf` | Baseline defaults for all games |
| Per-game config | `~/.config/vkBasalt-reloaded/games/<identifier>.conf` | Game-specific overrides |
| Presets | `~/.config/vkBasalt-reloaded/presets/<name>.conf` | Reusable parameter snapshots |

### Config Resolution Order

Parameters are resolved in this order (highest priority first):

1. **Per-game config** - Game-specific overrides
2. **Global config** - Baseline defaults
3. **Effect built-in defaults** - Hardcoded defaults in shader code

This means you can set `effects = crystalclear` globally, then override specific parameters per-game without duplicating the entire config.

Presets always include the active effect chain, making them fully self-contained and portable across different global configs.

### Per-Game Config Naming

vkBasalt-reloaded automatically detects games and creates unique config files:

**Steam games:**
```text
steam_<appid>_<GameName>.conf
```
Example: `steam_2357570_Overwatch.conf`

**Non-Steam games (Wine/Proton/Native):**
```text
<exename>_<md5hash>.conf
```
Example: `Unity_a1b2c3d4.conf`

The MD5 hash is computed from the full executable path, ensuring unique configs even for games with identical names (e.g., multiple Unity games).

**Game Registry:**
Non-Steam games are tracked in `~/.config/vkBasalt-reloaded/games/registry.conf`. This allows configs to survive:
- Game updates (binary changes, path stays same)
- Game moves (path changes, binary stays same)
- Wine prefix changes

### Config File Format

```ini
# Comments start with #
key=value
key = value          # spaces around = are fine
key="value with spaces"  # quotes supported

# Effect chain (colon-separated, processed left to right)
effects = smaa:crystalclear

# Boolean values
enableOnLaunch = true   # or: on, yes, 1
depthCapture = off      # or: no, false, 0
```

---

## Effect Chains

Effect chains define the post-processing pipeline. Effects are processed **left to right**, with each effect's output feeding into the next.

### Recommended Chains

```ini
# Daily driver: SMAA + CrystalClear (best quality)
# SMAA handles AA, CrystalClear adds contrast/clarity/grain
effects = smaa:crystalclear

# Lightweight: ClarityRCAS only (great for TAA cleanup, low-end systems)
# 5-tap RCAS + Clarity in a single pass, no AA
effects = clarityrcas

# Integrated FXAA: CrystalClear with built-in AA
# Decent FXAA (better than Nvidia's highest preset) at low cost
# Enable with: crystalclearEnableAA = 1
effects = crystalclear

# Maximum quality: Multi-pass with deband
effects = smaa:deband:crystalclear

# ReShade compatibility: Mix native and ReShade shaders
effects = smaa:colorfulness:crystalclear
```

### Performance Notes

- **SMAA** is the highest quality AA but has the highest cost.
- **CrystalClear's built-in FXAA** (`crystalclearEnableAA = 1`) reuses shader data for better performance than standalone FXAA.
- **ClarityRCAS** is the lightest option, ideal for handheld/low-end systems.
- Each additional effect adds ~0.1-0.3ms on modern GPUs.

---

## Global Configuration

### Core Settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `effects` | string | `crystalclear` | Colon-separated effect chain (left to right) |
| `enableOnLaunch` | bool | `true` | Apply effects on startup (can be toggled with hotkey) |
| `depthCapture` | bool | `off` | Enable depth buffer capture (experimental, may cause issues) |

### Hotkeys

| Key | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `toggleKey` | `string` | `Insert` | Toggle effects on/off |
| `reloadConfigKey` | `string` | `End` | Hot-reload config from disk |
| `overlayToggleKey` | `string` | `Home` | Open/close ImGui configuration overlay |
| `screenshotKey` | `string` | `Delete` | Capture screenshot of current frame |

All hotkeys support X11 keysym names. Common values: `Home`, `End`, `Insert`, `F1`-`F12`, `Pause`, etc.

### Overlay Settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `overlaySide` | string | `left` | Overlay snap position (`left` or `right`) |
| `overlayWidth` | float | `1080` | Overlay width in pixels (persisted on resize) |
| `cursorScale` | float | `0` | Mouse coordinate scale (0 = auto-detect). Only change if pointer misbehaves. |
| `uiScale` | float | `0` | UI element scale (0 = auto-detect). Scales padding/spacing/widgets, NOT mouse. |
| `fontScale` | float | `0` | Additional font size multiplier on top of uiScale. |

**Scale Auto-Detection:**
When set to `0`, the overlay detects scale from:
1. `cursorScale` config value (manual override)
2. KDE Plasma config (`~/.config/kwinrc` Scale=)
3. Qt/GDK environment variables (`QT_SCALE_FACTOR`, `GDK_SCALE`)
4. `wl_output` integer scale (Wayland)
5. Xft.dpi (X11)

### Theme Customization

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `themeBg` | string | `1a0d33` | Background color (hex, no #) |
| `themeAccent` | string | `47bf59` | Accent color (hex, no #) |
| `themeText` | string | `d9f2de` | Text color (hex, no #) |
| `themeBgAlpha` | float | `0.88` | Background opacity (0.0-1.0) |
| `themeRounding` | float | `3.0` | Frame corner rounding (0.0-12.0) |

All theme settings can be adjusted live in the overlay's Style tab.

### Screenshot Settings

| Key | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `screenshotFormat` | `string` | `png` | Output format: `png`, `jpg`, `bmp`, `tga`, `hdr` |
| `screenshotQuality` | `int` | `95` | JPEG quality (1-100). Ignored for non-JPEG formats. |
| `screenshotPath` | `string` | `(empty)` | Screenshot output directory. Empty = `$HOME/Pictures/vkBasalt-reloaded`. |
| `screenshotBeforeAfter` | `bool` | `false` | Save both raw game output (`_before`) and post-processed result (`_after`) |

Filenames include a timestamp and the detected game ID:
```text
20260813_152600_after_steam_1203220_NARAKA_BLADEPOINT.png
20260813_152600_before_steam_1203220_NARAKA_BLADEPOINT.png
```

### Format Details

| Format | Extension | Type | Notes |
| :--- | :--- | :--- | :--- |
| `png` | `.png` | Lossless | Default. Exact pixel reproduction. |
| `jpg` | `.jpg` | Lossy | Quality adjustable via `screenshotQuality`. |
| `bmp` | `.bmp` | Uncompressed | Maximum compatibility, large files. |
| `tga` | `.tga` | Uncompressed | Legacy/game engine import. |
| `hdr` | `.hdr` | Float RGBE | Radiance format. Source is LDR, values stored as `0.0`-`1.0` floats. |

---

## Shader Parameters

### CrystalClear

**All-in-one HDR-aware shader combining:**
- Anti-aliasing (FXAA)
- Macro contrast (Clarity-inspired) & Local Contrast
- Micro contrast (AMD CAS)
- Perceptual film grain
- Debanding & Tone curve
- Chroma smoothing & Fringe fixing (CA)
- Full Color Grading (CDL, Split Toning, Temp/Tint, Gamma/Black/White)

**Prefix:** `crystalclear`

#### Presets

```ini
# Choose a baseline preset (individual options override preset values)
# Options: devfav, esports, artifactless, maxsharp, vibrantsharp, devfxaa, cinematic, film, vivid, noir
crystalclearPreset = devfav
```

#### Core Toggles

```ini
# Enable built-in FXAA (reuses shader data, better performance than standalone FXAA)
crystalclearEnableAA = 0

# Enable RGB edge detection for AA (detects color edges, not just luma)
crystalclearEnableRGBEdgeDetection = 1
```

#### Sharpening & Contrast

Local contrast enhancement for cinematic "pop" and fine detail.

```ini
# Measurement distance for local contrast (higher = wider, more cinematic)
crystalclearBilateralRadius = 2.0       # Range: 0.5-8.0
crystalclearBilateralOffset = 1.5       # Range: 0.5-3.0

# Intensity of macro contrast (1.0 standard, up to 5.0+)
crystalclearSharpStrength = 1.0         # Range: 0.0-5.0

# Additional local contrast strength
crystalclearLocalContrastStrength = 0.0 # Range: 0.0-2.0

# Blend mode for applying contrast
# 0=Soft Light, 1=Overlay, 2=Hard Light, 3=Multiply, 4=Vivid, 5=Linear Light, 6=Addition
crystalclearBlendMode = 5               # Range: 0-6

# Mid-tone targeting (0-255). Excludes pure shadows/highlights from contrast enhancement.
crystalclearBlendIfDark = 40            # Range: 0-255
crystalclearBlendIfLight = 220          # Range: 0-255

# Internal CAS curve (0.0 = less sharp, 1.0 = max)
crystalclearCasSharpness = 0.8          # Range: 0.0-1.0

# Master CAS multiplier (1.0 standard, up to 5.0)
crystalclearCasStrength = 2.0           # Range: 0.0-5.0
```

#### Protection & Artifact Clearing

Control how aggressively the shader protects against artifacts. Lower values = more visible effect, higher values = more protection.

```ini
# Bilateral filter bounds (edge detection for contrast)
crystalclearEdgeThreshLow = 0.05        # Range: 0.0-1.0
crystalclearEdgeThreshHigh = 0.35       # Range: 0.0-1.0

# Suppress clarity on micro-textures (gravel, skin) to prevent crunch
crystalclearClarityTextureProtection = 0.5  # Range: 0.0-1.0

# Master multiplier for all protective masks
crystalclearGuardStrength = 0.6         # Range: 0.0-1.0

# Upper edge of local-contrast band-pass window
crystalclearBandPassWidth = 0.8         # Range: 0.3-1.5

# Back off at brightness extremes
crystalclearExtremeProtection = 0.5     # Range: 0.0-1.0

# Isolation/shimmer averaging gate strength
crystalclearShimmerReduction = 0.5      # Range: 0.0-1.0

# Blurs only chroma channels in flat areas to kill color noise without softening luma
crystalclearEnableChromaSmooth = 0      # Range: 0-1
crystalclearChromaSmoothStrength = 0.5  # Range: 0.0-1.0

# Despeckle filter for isolated bright/dark pixel noise
crystalclearEnableDespeckle = 0         # Range: 0-1
crystalclearDespeckleThreshold = 0.15   # Range: 0.0-1.0

# Chromatic Aberration (Fringe) Fix
crystalclearEnableFringeFix = 0         # Range: 0-1
crystalclearFringeStrength = 0.5        # Range: 0.0-1.0
```

#### FXAA (Built-in Anti-Aliasing)

```ini
# Enable built-in FXAA (reuses shader data, better performance than standalone FXAA)
crystalclearEnableAA = 0            # Range: 0-1

# Enable RGB edge detection (detects color edges, not just luma)
crystalclearEnableRGBEdgeDetection = 1  # Range: 0-1

# Min contrast for edge detection (lower = more AA/blur, higher = sharper)
crystalclearFxaaEdgeThreshold = 0.05    # Range: 0.001-1.0

# Dark scene floor (prevents AA from blurring noise in deep shadows)
crystalclearFxaaEdgeThresholdMin = 0.0312 # Range: 0.0-1.0

# Subpixel smoothing (thin wires/hair). 1.0 = soft, 0.5 = sharp, 0.0 = off
crystalclearFxaaSubpixAmount = 1.0      # Range: 0.0-1.0

# Edge walk step size (keep at 1.0)
crystalclearFxaaSearchScale = 1.0       # Range: 0.1-3.0

# Min perpendicular contrast to start edge walk
crystalclearFxaaHardEdgeThreshold = 0.08  # Range: 0.0-1.0

# Debug: Bypass other effects, apply only FXAA
crystalclearFxaaOnlyMode = 0            # Range: 0-1
```

#### Color & Tone

```ini
# Non-linear saturation boost (boosts less saturated colors more). Preserves luma.
crystalclearVibrance = 0.0              # Range: -1.0 to 1.0

# Breaks up color banding in flat/gradient areas (skies, fog, shadows)
crystalclearEnableDeband = 0            # Range: 0-1
crystalclearDebandStrength = 0.5        # Range: 0.0-1.0

# Filmic highlight rolloff (softly compresses bright highlights)
crystalclearToneCurve = 0.0             # Range: 0.0-1.0

# Desaturates extreme specular highlights toward white (mimics real lens behavior)
crystalclearSpecularDesat = 0.0         # Range: 0.0-1.0
```

#### Color Grading

Professional color grading suite. Operates on HDR-normalized values for HDR consistency.

```ini
# Standard Saturation (-1.0 = grayscale, 0.0 = off, 1.0 = max)
crystalclearSaturation = 0.0            # Range: -1.0 to 1.0

# ASC Color Decision List (CDL)
crystalclearEnableCDL = 0               # Range: 0-1
crystalclearCDLSlopeR = 1.0             # Range: 0.0-4.0 (Contrast/Gain)
crystalclearCDLSlopeG = 1.0             # Range: 0.0-4.0
crystalclearCDLSlopeB = 1.0             # Range: 0.0-4.0
crystalclearCDLOffsetR = 0.0            # Range: -1.0 to 1.0 (Lift/Bias)
crystalclearCDLOffsetG = 0.0            # Range: -1.0 to 1.0
crystalclearCDLOffsetB = 0.0            # Range: -1.0 to 1.0
crystalclearCDLPowerR = 1.0             # Range: 0.1-4.0 (Gamma)
crystalclearCDLPowerG = 1.0             # Range: 0.1-4.0
crystalclearCDLPowerB = 1.0             # Range: 0.1-4.0

# Split Toning (Colorize shadows and highlights independently)
crystalclearEnableSplitTone = 0         # Range: 0-1
crystalclearSplitToneStrength = 0.0     # Range: 0.0-1.0
crystalclearSTShadowR = 0.0             # Range: 0.0-1.0 (Shadow Hue RGB)
crystalclearSTShadowG = 0.5             # Range: 0.0-1.0
crystalclearSTShadowB = 0.5             # Range: 0.0-1.0
crystalclearSTHighR = 0.5               # Range: 0.0-1.0 (Highlight Hue RGB)
crystalclearSTHighG = 0.3               # Range: 0.0-1.0
crystalclearSTHighB = 0.0               # Range: 0.0-1.0

# White Balance
crystalclearTemperature = 0.0           # Range: -1.0 to 1.0 (Blue/Yellow)
crystalclearTint = 0.0                  # Range: -1.0 to 1.0 (Green/Magenta)

# Tone Response Shaping
crystalclearGammaAdjust = 0.0           # Range: -0.9 to 0.9 (Perceptual brightness curve)
crystalclearBlackLift = 0.0             # Range: 0.0-0.5 (Raises black floor for faded film look)
crystalclearWhiteClip = 0.0             # Range: 0.0-0.5 (Lowers white ceiling)
```

#### Perceptual Film Grain & Dithering

Uses 4-layer masking (Luma, Texture, Edge, Sharpening awareness) for organic grain.

```ini
# Temporal dithering (reduces banding)
crystalclearEnableDithering = 1         # Range: 0-1

# Film Grain Master Toggle & Intensity
crystalclearEnableFilmGrain = 1         # Range: 0-1
crystalclearFilmGrainStrength = 1.0     # Range: 0.0-2.0
crystalclearFilmGrainMinimum = 0.0      # Range: 0.0-2.0 (Floor amount)

# Fine grain: 1:1 pixel resolution, sharp, high-ISO digital feel
crystalclearFineGrainWeight = 0.4       # Range: 0.0-1.0

# Coarse grain: 1/4th resolution, heavy, cinematic 35mm clumps
crystalclearCoarseGrainWeight = 0.8     # Range: 0.0-1.0
```

#### Debug Overlays

```ini
# Isolate specific effect passes for debugging
crystalclearEnableDebugAA = 0           # Range: 0-1 (Red)
crystalclearEnableDebugCAS = 0          # Range: 0-1 (Green)
crystalclearEnableDebugClarity = 0      # Range: 0-1 (Blue)
crystalclearEnableDebugGrain = 0        # Range: 0-1 (Cyan)
```

---

### ClarityRCAS

**Lightweight alternative to CrystalClear:** 5-tap RCAS + Clarity in a single pass. Great for TAA cleanup on low-end systems.

**Prefix:** `clarityR`

```ini
clarityRStrength = 1.0                  # Macro contrast intensity (Range: 0.0-5.0)
clarityRBilateralRadius = 2.0           # Measurement distance (Range: 0.5-8.0)
clarityRBilateralOffset = 1.5           # Measurement offset (Range: 0.5-3.0)
clarityRcasSharpness = 0.8              # CAS sharpness (Range: 0.0-2.0)
clarityRcasStrength = 1.0               # CAS strength (Range: 0.0-5.0)
clarityRBlendIfDark = 40                # Mid-tone dark bound (Range: 0-255)
clarityRBlendIfLight = 220              # Mid-tone light bound (Range: 0-255)
clarityREdgeThreshLow = 0.05            # Bilateral low bound (Range: 0.0-1.0)
clarityREdgeThreshHigh = 0.35           # Bilateral high bound (Range: 0.0-1.0)
clarityREnableDithering = 1             # Range: 0-1
clarityREnableFilmGrain = 1             # Range: 0-1
clarityRFilmGrainStrength = 1.0         # Range: 0.0-2.0
clarityRFilmGrainMinimum = 0.0          # Range: 0.0-2.0
clarityRFineGrainWeight = 0.4           # Range: 0.0-1.0
clarityRCoarseGrainWeight = 0.8         # Range: 0.0-1.0
```

---

### Clarity

**Standard 9-tap local contrast enhancement.** Simpler than CrystalClear/ClarityRCAS, good for basic sharpening.

**Prefix:** `clarity`

```ini
clarityStrength = 1.0                   # Macro contrast intensity (Range: 0.0-5.0)
clarityRadius = 2                       # Sampling radius (Range: 1-8)
clarityOffset = 1.5                     # Sampling offset (Range: 0.5-3.0)
clarityEnableDithering = 1              # Range: 0-1
clarityBlendMode = 5                    # 0=Soft Light, 1=Overlay, 2=Hard Light, 3=Multiply, 4=Vivid, 5=Linear Light, 6=Addition
clarityBlendIfDark = 40                 # Range: 0-255
clarityBlendIfLight = 220               # Range: 0-255
clarityEdgeThreshLow = 0.05             # Range: 0.0-1.0
clarityEdgeThreshHigh = 0.35            # Range: 0.0-1.0
```

---

### SMAA

**Subpixel Morphological Anti-Aliasing.** Highest quality PostFX AA, best combined with CrystalClear or ClarityRCAS.

**Prefix:** `smaa`

```ini
smaaEdgeDetection = color               # Options: color, luma
smaaThreshold = 0.05                    # Edge detection threshold (Range: 0.01-0.5)
smaaMaxSearchSteps = 32                 # Max search steps (Range: 0-112)
smaaMaxSearchStepsDiag = 16             # Diagonal search steps (Range: 0-20)
smaaCornerRounding = 0                  # Corner rounding (Range: 0-100)
```

---

### Standalone Effects

#### CAS (AMD FidelityFX Contrast Adaptive Sharpening)
**Prefix:** `cas`
```ini
casSharpness = 0.8                      # Sharpness intensity (Range: 0.0-1.0)
```

#### FXAA (Fast Approximate Anti-Aliasing)
**Prefix:** `fxaa`
```ini
fxaaEdgeThreshold = 0.05                # Range: 0.001-1.0
fxaaEdgeThresholdMin = 0.0312           # Range: 0.0-1.0
fxaaSubpixAmount = 0.75                 # Range: 0.0-1.0
fxaaSearchScale = 1.0                   # Range: 0.1-3.0
fxaaHardEdgeThreshold = 0.08            # Range: 0.0-1.0
```

#### Deband
**Prefix:** `deband`
```ini
debandStrength = 0.5                    # Range: 0.0-1.0
```

#### LUT (Color grading via Look-Up Table)
**Prefix:** `lut`
```ini
lutFile = /path/to/lut.png              # PNG format, 256x16 or 512x512
```

#### DLS (Denoised Luma Sharpening)
*Not recommended for TAA enabled games as it can exaggerate artifacts.*
**Prefix:** `dls`
```ini
dlsStrength = 1.0                       # Range: 0.0-5.0
```

---

### ReShade Effects

vkBasalt-reloaded supports most single-technique ReShade FX shaders. Place them in your config as shown:

```ini
effects = colorfulness:denoise

# Map effect names to shader file paths
colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx

# Texture and include paths for ReShade shaders
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders
```

**Limitations:**
- Multi-technique shaders may not work.
- Depth buffer access isn't fully supported (`depthCapture = on` exists but is experimental).
- To modify ReShade shader parameters, edit the `.fx` file directly.

---

## Adding New Effects

*For developers who want to add custom shaders to vkBasalt-reloaded. (PRs welcome)*

### Step 1: Create the Effect Class
Create `src/effect_myshader.hpp` and `src/effect_myshader.cpp`. Inherit from `SimpleEffect`. The base class automatically handles the optimized 3-barrier chain-aware pipeline layout.

```cpp
// src/effect_myshader.hpp
#pragma once
#include "effect_simple.hpp"

namespace vkBasalt {
    class MyShaderEffect : public SimpleEffect {
    public:
        MyShaderEffect(LogicalDevice* pLogicalDevice, VkFormat format,
                       VkExtent2D imageExtent, std::vector<VkImage> inputImages,
                       std::vector<VkImage> outputImages, Config* pConfig);
        ~MyShaderEffect();
        
        std::string getName() const override { return "myshader"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;
    };
}
```

### Step 2: Declare Parameters
Override `getParamDescs()` to return a static vector of `EffectParamDesc`. The ImGui overlay will automatically generate the UI, handle Save/Load, and integrate with the Preset system.

```cpp
const std::vector<EffectParamDesc>& MyShaderEffect::getParamDescs() const {
    static const std::vector<EffectParamDesc> params = {
        {"myshaderStrength", "Strength", ParamType::Float, 1.0, 0.0, 5.0, 0.1, {}, "General"},
        {"myshaderEnableFeature", "Enable Feature", ParamType::Bool, 0.0, 0.0, 1.0, 1.0, {}, "General"},
    };
    return params;
}
```

### Step 3: Register the Effect
In `src/effect_chain.cpp`, add a branch in `buildEffectChain`:
```cpp
else if (effectStrings[i] == "myshader") {
    pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
        new MyShaderEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent,
                           firstImages, secondImages, pConfig)));
}
```

### Step 4: Add to meson.build
Add your `.cpp` file to the `vkBasalt_src` list in `src/meson.build`.

### Step 5: Read Parameters in Constructor
```cpp
MyShaderEffect::MyShaderEffect(...) {
    float strength = pConfig->getOption<float>("myshaderStrength", 1.0f);
    m_paramValues["myshaderStrength"] = strength;
    
    // Build specialization constants from m_paramValues...
    
    init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
}
```

---

## Quick Reference

### Common Configurations

```ini
# Best quality (SMAA + CrystalClear)
effects = smaa:crystalclear
crystalclearPreset = devfav

# Best quality, good in-game AA exists, game looks dull
effects = crystalclear
crystalclearPreset = devfav
crystalclearVibrance = 0.3

# Lightweight (ClarityRCAS only)
effects = clarityrcas
clarityRcasStrength = 1.0

# Competitive gaming (minimal latency)
effects = cas
casSharpness = 0.5
```

---

## Troubleshooting

### Game won't launch
Ensure that you're using `ENABLE_VKBASALT_RELOADED=1` and NOT `ENABLE_VKBASALT=1`. Some Proton versions bundle the old upstream vkBasalt by default, which lacks our custom effects and will halt Vulkan.

### Artifacts / Crunchy Sharpening
- Try a different preset: `crystalclearPreset = artifactless`
- Lower sharpness: `crystalclearSharpStrength = 0.5`
- Increase protection: `crystalclearGuardStrength = 0.8`
- Disable film grain: `crystalclearEnableFilmGrain = 0`

### Overlay not responding
- Verify hotkey isn't conflicting with game controls.
- Try different hotkey: `overlayToggleKey = F10`
- Try using the overlay with an in-game cursor on (Menu, map, etc.) as software cursor fallback.

### Performance issues
- Use a lighter effect chain: `effects = clarityrcas`
- Reduce sharpness values.
- Disable film grain, dithering, and debanding.
- Check GPU utilization with `mangohud`. Each setting you turn off yields extra FPS.

---

## Support

- **GitHub Issues**: [vkBasalt-reloaded/issues](https://github.com/Skyrion9/vkBasalt-reloaded/issues)
- **Logging**: Set env flag `VKBASALT_LOG_LEVEL=debug` (trace, debug, info, warn, error, none). Output goes to stderr or `VKBASALT_LOG_FILE="vkBasalt.log"`.
- **Contributions**: PRs for new shaders, optimizations, and features are highly welcome! You can also donate using addresses in **[Readme](README.md)**