
---

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
```
steam_<appid>_<GameName>.conf
```
Example: `steam_2357570_Overwatch.conf`

**Non-Steam games (Wine/Proton/Native):**
```
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

- **SMAA** is the highest quality AA but has the highest cost
- **CrystalClear's built-in FXAA** (`crystalclearEnableAA = 1`) reuses shader data for better performance than standalone FXAA
- **ClarityRCAS** is the lightest option, ideal for handheld/low-end systems
- Each additional effect adds ~0.1-0.3ms on modern GPUs

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
| `screenshotPath` | `string` | `(empty)` | Screenshot output directory. Empty = `$HOME`. |
| `screenshotBeforeAfter` | `bool` | `false` | Save both raw game output (`_before`) and post-processed result (`_after`) |


Filenames include a timestamp and the detected game ID:
```
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
- Macro contrast (Clarity-inspired)
- Micro contrast (AMD CAS)
- Perceptual film grain
- Debanding
- Tone curve
- Chroma smoothing

**Prefix:** `crystalclear`

#### Presets

```ini
# Choose a baseline preset (individual options override preset values)
# Options: devfav, esports, artifactless, maxsharp, vibrantsharp, devfxaa, cinematic
crystalclearPreset = devfav
```

#### Core Toggles

```ini
# Enable built-in FXAA (reuses shader data, better performance than standalone FXAA)
crystalclearEnableAA = 0

# Enable RGB edge detection for AA (detects color edges, not just luma)
crystalclearEnableRGBEdgeDetection = 1
```

#### Macro Contrast (Clarity)

Local contrast enhancement for cinematic "pop".

```ini
# Measurement distance for local contrast (higher = wider, more cinematic)
crystalclearBilateralRadius = 2.0       # Range: 0.5-8.0
crystalclearBilateralOffset = 1.5       # Range: 0.5-3.0

# Intensity of macro contrast (1.0 standard, up to 5.0+)
crystalclearSharpStrength = 1.0         # Range: 0.0-5.0

# Blend mode for applying contrast
# 0=Soft Light (smooth), 1=Overlay (pop), 2=Hard Light, 3=Multiply, 4=Vivid, 5=Linear Light, 6=Addition
crystalclearBlendMode = 5               # Range: 0-6

# Mid-tone targeting (0-255). Excludes pure shadows/highlights from contrast enhancement.
crystalclearBlendIfDark = 40            # Range: 0-255
crystalclearBlendIfLight = 220          # Range: 0-255

# Bilateral filter bounds (edge detection for contrast)
# Low = blur start threshold, High = blur cross threshold (pop intensity)
crystalclearEdgeThreshLow = 0.05        # Range: 0.0-1.0
crystalclearEdgeThreshHigh = 0.35       # Range: 0.0-1.0

# Suppress clarity on micro-textures (gravel, skin) to prevent crunch
# 0.0 = off, 0.5 = balanced, 1.0 = max protection
crystalclearClarityTextureProtection = 0.5  # Range: 0.0-1.0
```

#### Micro Contrast (CAS)

AMD FidelityFX Contrast Adaptive Sharpening for fine detail.

```ini
# Internal CAS curve (0.0 = less sharp, 1.0 = max)
crystalclearCasSharpness = 0.8          # Range: 0.0-1.0

# Master CAS multiplier (1.0 standard, up to 5.0)
crystalclearCasStrength = 2.0           # Range: 0.0-5.0
```

#### FXAA (Built-in Anti-Aliasing)

```ini
# Min contrast for edge detection (lower = more AA/blur, higher = sharper/more aliasing)
crystalclearFxaaEdgeThreshold = 0.05    # Range: 0.001-1.0

# Dark scene floor (prevents AA from blurring noise in deep shadows)
crystalclearFxaaEdgeThresholdMin = 0.0312  # Range: 0.0-1.0

# Subpixel smoothing (thin wires/hair). 1.0 = soft, 0.5 = sharp, 0.0 = off
crystalclearFxaaSubpixAmount = 0.75     # Range: 0.0-1.0

# Edge walk step size (keep at 1.0)
crystalclearFxaaSearchScale = 1.0       # Range: 0.1-3.0

# Min perpendicular contrast to start edge walk (prevents artifacts on soft gradients)
crystalclearFxaaHardEdgeThreshold = 0.08  # Range: 0.0-1.0

# Debug: Bypass other effects, apply only FXAA
crystalclearFxaaOnlyMode = 0            # Range: 0-1
```

#### Color Enhancement

```ini
# Non-linear saturation boost (boosts less saturated colors more than already saturated ones)
# Preserves luma (brightness). 0.0 = off, 0.5 = noticeable pop, 1.0 = max. Negative = desaturate.
crystalclearVibrance = 0.0              # Range: -1.0 to 1.0
```

#### Debanding

Breaks up color banding in flat/gradient areas (skies, fog, shadows).

```ini
# Enable debanding
crystalclearEnableDeband = 0            # Range: 0-1

# Intensity of debanding (0.0 = off, 1.0 = max)
crystalclearDebandStrength = 0.5        # Range: 0.0-1.0
```

#### Tone Curve

```ini
# Filmic highlight rolloff (softly compresses bright highlights to prevent harsh clipping)
# 0.0 = off, 1.0 = max
crystalclearToneCurve = 0.0             # Range: 0.0-1.0
```

#### Chroma Smoothing (Color Denoise)

Blurs only chroma channels in flat areas to kill color noise from TAA/compression without softening luma detail.

```ini
# Enable chroma smoothing
crystalclearEnableChromaSmooth = 0      # Range: 0-1

# Intensity of chroma smoothing (0.0 = off, 0.5 = balanced, 1.0 = max)
crystalclearChromaSmoothStrength = 0.5  # Range: 0.0-1.0
```

#### Specular Desaturation

```ini
# Desaturates extreme specular highlights toward white (mimics real lens behavior)
# Prevents "neon" highlights. 0.0 = off, 0.4 = subtle, 1.0 = max
crystalclearSpecularDesat = 0.0         # Range: 0.0-1.0
```

#### Perceptual Film Grain

Uses 4-layer masking (Luma, Texture, Edge, Sharpening awareness) for organic, non-distracting grain.

```ini
# Enable film grain
crystalclearEnableFilmGrain = 1         # Range: 0-1

# Overall intensity
crystalclearFilmGrainStrength = 1.0     # Range: 0.0-2.0

# Floor amount applied regardless of masks
crystalclearFilmGrainMinimum = 0.0      # Range: 0.0-2.0

# Fine grain: 1:1 pixel resolution, updates every frame (sharp, high-ISO digital feel)
crystalclearFineGrainWeight = 0.4       # Range: 0.0-1.0

# Coarse grain: 1/4th resolution, updates every 2 frames (heavy, cinematic 35mm clumps)
crystalclearCoarseGrainWeight = 0.8     # Range: 0.0-1.0
```

#### Guard & Masking Controls

Control how aggressively the shader protects against artifacts. Lower values = more visible effect, higher values = more protection.

```ini
# Master multiplier for all protective masks (0.0 = off, 1.0 = full protection)
crystalclearGuardStrength = 0.6         # Range: 0.0-1.0

# Upper edge of local-contrast band-pass window (wider = effect applies to more contrast levels)
crystalclearBandPassWidth = 0.8         # Range: 0.3-1.5

# Back off at brightness extremes (0.0 = sharpen everything equally, 1.0 = full protection)
crystalclearExtremeProtection = 0.5     # Range: 0.0-1.0

# Isolation/shimmer averaging gate strength (0.0 = off, 1.0 = full suppression)
crystalclearShimmerReduction = 0.5      # Range: 0.0-1.0
```

#### Dithering & Debug

```ini
# Enable temporal dithering (reduces banding)
crystalclearEnableDithering = 1         # Range: 0-1

# Debug overlays: Red=FXAA, Green=CAS, Blue=Clarity, Cyan=Grain Mask
crystalclearEnableDebugAA = 0           # Range: 0-1
crystalclearEnableDebugCAS = 0          # Range: 0-1
crystalclearEnableDebugClarity = 0      # Range: 0-1
crystalclearEnableDebugGrain = 0        # Range: 0-1
```

---

### ClarityRCAS

**Lightweight alternative to CrystalClear:** 5-tap RCAS + Clarity in a single pass. Great for TAA cleanup on low-end systems.

**Prefix:** `clarityR`

```ini
# Intensity of macro contrast (1.0 standard, up to 5.0+)
clarityRStrength = 1.0                  # Range: 0.0-5.0

# Measurement distance for local contrast (higher = wider, more cinematic)
clarityRBilateralRadius = 2.0           # Range: 0.5-8.0
clarityRBilateralOffset = 1.5           # Range: 0.5-3.0

# CAS sharpness (0.0 = less sharp, 1.0 = max)
clarityRcasSharpness = 0.8              # Range: 0.0-2.0

# CAS strength (1.0 standard, >2.0 may cause crunchy artifacts)
clarityRcasStrength = 1.0               # Range: 0.0-5.0

# Mid-tone targeting (0-255)
clarityRBlendIfDark = 40                # Range: 0-255
clarityRBlendIfLight = 220              # Range: 0-255

# Bilateral filter bounds
clarityREdgeThreshLow = 0.05            # Range: 0.0-1.0
clarityREdgeThreshHigh = 0.35           # Range: 0.0-1.0

# Dithering
clarityREnableDithering = 1             # Range: 0-1

# Film grain (same parameters as CrystalClear)
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
# Intensity of macro contrast
clarityStrength = 1.0                   # Range: 0.0-5.0

# Radius/distance from center pixel where sampling begins
clarityRadius = 2                       # Range: 1-8
clarityOffset = 1.5                     # Range: 0.5-3.0

# Dithering
clarityEnableDithering = 1              # Range: 0-1

# Blend mode (0-6, see CrystalClear for descriptions)
clarityBlendMode = 5                    # Range: 0-6

# Mid-tone targeting
clarityBlendIfDark = 40                 # Range: 0-255
clarityBlendIfLight = 220               # Range: 0-255

# Edge detection thresholds
clarityEdgeThreshLow = 0.05             # Range: 0.0-1.0
clarityEdgeThreshHigh = 0.35            # Range: 0.0-1.0
```

---

### SMAA

**Subpixel Morphological Anti-Aliasing.** Highest quality PostFX AA, best combined with CrystalClear or ClarityRCAS.

**Prefix:** `smaa`

```ini
# Edge detection mode: 'color' catches macro edges better than 'luma'
smaaEdgeDetection = color               # Options: color, luma

# Edge detection threshold (lower = more sensitive, catches more edges)
smaaThreshold = 0.05                    # Range: 0.01-0.5

# Maximum search steps for edge patterns (higher = better quality, slower)
smaaMaxSearchSteps = 32                 # Range: 0-112

# Diagonal search steps (catches diagonal edges)
smaaMaxSearchStepsDiag = 16             # Range: 0-20

# Corner rounding (0 = preserves razor-sharp geometric corners, 100 = max rounding)
smaaCornerRounding = 0                  # Range: 0-100
```

---

### CAS

**AMD FidelityFX Contrast Adaptive Sharpening (standalone).** Use if you want CAS without Clarity or CrystalClear.

**Prefix:** `cas`

```ini
# Sharpness intensity
casSharpness = 0.8                      # Range: 0.0-1.0
```

---

### FXAA

**Fast Approximate Anti-Aliasing (standalone).** Use if you want FXAA without CrystalClear's integrated version.

**Prefix:** `fxaa`

```ini
# Edge detection threshold
fxaaEdgeThreshold = 0.05                # Range: 0.001-1.0
fxaaEdgeThresholdMin = 0.0312           # Range: 0.0-1.0

# Subpixel smoothing
fxaaSubpixAmount = 0.75                 # Range: 0.0-1.0

# Search scale
fxaaSearchScale = 1.0                   # Range: 0.1-3.0

# Hard edge threshold
fxaaHardEdgeThreshold = 0.08            # Range: 0.0-1.0
```

---

### Deband

**Standalone debanding shader.** Use if you want debanding without CrystalClear's integrated version.

**Prefix:** `deband`

```ini
# Debanding strength
debandStrength = 0.5                    # Range: 0.0-1.0
```

---

### LUT

**Color grading via Look-Up Table.** Apply custom color transformations.

**Prefix:** `lut`

```ini
# Path to LUT file (PNG format, 256x16 or 512x512)
lutFile = /path/to/lut.png
```

---

### DLS

**Denoised Luma Sharpening.** Alternative sharpening algorithm. I wouldn't recommend using this in TAA enabled games as it can exaggerate artifacts.

**Prefix:** `dls`

```ini
# Sharpening strength
dlsStrength = 1.0                       # Range: 0.0-5.0
```

---

### ReShade Effects

vkBasalt-reloaded supports most single-technique ReShade FX shaders. However it's not a priority to maintain compatibility as many things can go wrong and Reshade updates often. Place them in your config as shown:

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
- Multi-technique shaders may not work
- Depth buffer access isn't supported (`depthCapture = on`) exists however.
- Some blending/stencil operations may have issues

To modify ReShade shader parameters, edit the `.fx` file directly.
- This might change in the future, but playing catch up with Reshade is a lot of work so I'd rather have a roster of high performance shaders in GLSL, compute shaders and other more important features before chasing that.

---

## Adding New Effects

*For developers who want to add custom shaders to vkBasalt-reloaded. (PRs welcome)*

### Step 1: Create the Effect Class

Create `src/effect_myshader.hpp` and `src/effect_myshader.cpp`. Inherit from `SimpleEffect`.

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
        
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        
        // Declarative parameter interface
        std::string getName() const override { return "myshader"; }
        const std::vector<EffectParamDesc>& getParamDescs() const override;
    };
}
```

### Step 2: Declare Parameters

Override `getParamDescs()` to return a static vector of `EffectParamDesc`:

```cpp
const std::vector<EffectParamDesc>& MyShaderEffect::getParamDescs() const {
    static const std::vector<EffectParamDesc> params = {
        {"myshaderStrength", "Strength", ParamType::Float, 1.0, 0.0, 5.0, 0.1},
        {"myshaderEnableFeature", "Enable Feature", ParamType::Bool, 0.0, 0.0, 1.0, 1.0},
        {"myshaderMode", "Mode", ParamType::Int, 0.0, 0.0, 3.0, 1.0},
    };
    return params;
}
```

The ImGui overlay will automatically:
- Show your effect in the left panel
- Generate sliders/checkboxes/combos for each parameter
- Handle Save/Load/Preset integration

### Step 3: Register the Effect

In `src/effect_chain.cpp`, add a branch in `buildEffectChain`:

```cpp
else if (effectStrings[i] == "myshader")
{
    pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
        new MyShaderEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent,
                           firstImages, secondImages, pConfig)));
    Logger::debug("created MyShaderEffect");
}
```

### Step 4: Add to meson.build

Add your `.cpp` file to the `vkBasalt_src` list in `src/meson.build`:

```meson
vkBasalt_src = [
    # ... existing files ...
    'effect_myshader.cpp',
]
```

### Step 5: Read Parameters in Constructor

```cpp
MyShaderEffect::MyShaderEffect(...) {
    // Read from config, store in m_paramValues for live tweaking
    float strength = pConfig->getOption<float>("myshaderStrength", 1.0f);
    m_paramValues["myshaderStrength"] = strength;
    
    // Build specialization constants from m_paramValues
    // ... your shader setup code ...
    
    init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
}
```

### Maintenance Rule

Adding a new parameter to an existing effect = **one line** in the `getParamDescs()` array + one line in the constructor to read it. No ImGui code changes needed.

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

# Integrated FXAA (CrystalClear with built-in AA)
effects = crystalclear
crystalclearEnableAA = 1
crystalclearPreset = devfxaa

# Competitive gaming (minimal latency)
effects = cas
casSharpness = 0.5
```

### Overlay Navigation

| Key | Action |
| :--- | :--- |
| `Mouse` | All of the below |
| Drag left / right | Adjust sliders/numeric fields incrementally |
| `Tab` / `Arrows` | Navigate widgets |
| `Enter` | Activate / Edit (click a drag field to type exact values) |
| `Left` / `Right` | Adjust focused value |
| `Space` | Toggle checkbox |
| `Shift` + `Left` / `Right` | Cycle tabs |
| `/` | Focus search box |
| `Esc` | Close overlay |

### Window Behavior

- **Draggable** with edge-snapping (blue highlight zones appear while dragging)
- **Full-height**, resizable width (persisted across sessions)
- **Unsaved changes warning** on close
- **Mouse & Keybaord optimized** draggable widgets (drag left/right to change values, click to type exact numbers)
---

## Troubleshooting

### Game won't launch

Ensure that you're using `ENABLE_VKBASALT_RELOADED=1` and NOT `ENABLE_VKBASALT=1` env flag as some proton versions install the old upstream vkbasalt by default.
```bash
# Remove old installations
sudo rm -f /usr/share/vulkan/implicit_layer.d/vkBasalt.json
sudo rm -f /usr/local/share/vulkan/implicit_layer.d/vkBasalt.json
sudo rm -f /usr/lib/libvkbasalt.so
sudo rm -f /usr/lib/x86_64-linux-gnu/libvkbasalt.so
```

### Artifacts

- Try a different preset: `crystalclearPreset = artifactless`
- Lower sharpness: `crystalclearSharpStrength = 0.5`
- Disable film grain: `crystalclearEnableFilmGrain = 0`
- Increase protection: `crystalclearGuardStrength = 0.8`

### Overlay not responding

- Check Wayland/X11 input is working: `ENABLE_VKBASALT_RELOADED=1 vkcube`
- Verify hotkey isn't conflicting with game controls
- Try different hotkey: `overlayToggleKey = F10`
- Try using the overlay with in-game cursor on (Menu, map etc.) as software cursor can be buggy.

### Performance issues

- Use lighter effect chain: `effects = clarityrcas`
- Reduce sharpness values
- Disable film grain, dithering and debanding can be cheaper.
- Check GPU utilization with `mangohud` as you tweak settings. Each setting you turn off is extra FPS.

---

## Support

- **GitHub Issues**: [vkBasalt-reloaded/issues](https://github.com/Skyrion9/vkBasalt-reloaded/issues)
- **Discussions**: Share configs and presets with the community
- **Contributions**: PRs for new shaders, optimizations and features are welcome!

---

