# vkBasalt-reloaded

vkBasalt-reloaded builds on the legacy of vkBasalt to bring you new shaders and layer-level optimizations.

**Why this fork?** This fork aims to bring wayland toggle support, increase performance and curate a collection of highly optimized, native GLSL shaders that drastically outperform ReShade. For example, running `Clarity.fx` + `CAS` through ReShade costs me ~20 Watts and at times requires upscaling. Running the native `CrystalClear` and `ClarityRCAS` shaders in vkBasalt costs just 5-10 Watts at 4K. Besides we don't have to manage wine prefixes or install per-game like reshade requires. This efficiency focused approach is also friendly to handhelds. (Steam Deck, ROG Ally, etc.).

Contributions, PRs, and new shader ports are highly welcome!

Note that maintaining Reshade part of vkbasalt is on the backburner as it's a catch-up game and I'd rather get a stable codebase before increasing the scope. Such few reshade shaders work with vkbasalt to begin with anyways it might just be easier to port them over lol.

The priorities for this fork:
1. Maintaining a highly performant codebase with optimized suite of highly configurable shaders.
2. Implementing a proper Dear ImGui UI to allow per-game configuration in a stable manner.
3. Compute shader support, as this could increase SMAA performance (Arguably the best AA method in PostFX) by a good margin and allows running a wider array of shaders.

I want to avoid playing catch up with reshade?.. It's a difficult task with how fast reshade is updating, so it's the lowest priority for me. Occasionally reshade might break too as I don't actively test it. However contributions are welcome in this aspect regardless, or issue reports if an effect that worked in original vkBasalt is broken here.


---

## 🌟 Custom Shaders & Engine Enhancements

### Quality/Performance Optimized Shaders
*   **`crystalclear`**: Singlepass heuristical filter. Combines FXAA (Disabled by default), CAS, Macro Contrast (Clarity-inspired), and Perceptual Film Grain into a **single, highly optimized pass**. Features advanced artifact protection, UI/Text choking, and hybrid multi-scale procedural grain driven by Vulkan UBO. Basically, this shader alone is a suite of highly optimized (Image quality and performance) shaders. Carefully hand-tuned for a delicate balance of 'pop' and minimal artifacts. YMMV between games which is why you've access to ~29 configuration options for this shader alone in `vkBasalt.conf`
*   **`clarityrcas`**: Another optimized singlepass shader. 5-tap anchored local contrast enhancement hybridized with AMD's FSR RCAS. Uses Gaussian bilateral weights, symmetrical clamping, and band-pass protection for zero-halo micro-sharpening. Ideal for cleaning up smudgy TAA or Frame Generation artifacts for lower end systems. This is a lite version of CrystalClear essentially.
*   **`clarity`**: An optimized single-pass 9-tap cross-convolution local contrast enhancement. Inspired by Reshade's Clarity.fx, this lightweight, singlepass version delivers macro contrast with a fraction of the processing cost.

### Core Vulkan Engine Fixes (vs. Upstream)
*   **Hot-Reload Support**: Press `End` to reload `vkBasalt.conf` in real-time, you can safely add, remove or reconfigure effects during runtime, no longer need to restart your game to see changes.
*   **Synchronization Overhaul**: Pipeline barriers and access masks slimmed down instead of using all-catching parameters we only fetch and deliver what is needed, increasing efficiency and reducing performance cost. Reducing GPU stalls and bandwidth usage.
*   **Wayland Support**: Added native Wayland keyboard input hooks for toggle/reload hotkeys.
*   **Tile-Based GPU Optimizations**: Implemented `VK_ATTACHMENT_LOAD_OP_DONT_CARE` across single-pass effects to maximize mobile/handheld/lowend GPU performance, with explicit clear overrides for multi-pass effects like SMAA.
*   **Temporal UBO Architecture**: Added Vulkan Uniform Buffer Object support for per-frame temporal data for e.g. truly randomized temporal film grain.

---

## 🛠️ Building & Installation

### Automated Scripts (Recommended)
These scripts automatically apply `-march=native`, ThinLTO, O3, C++20, and assertion stripping. They will prompt for `sudo` to install system-wide and patch the Vulkan manifest for native Proton support.
Note that this stripping might break reshade? I haven't tested it but issues are welcome.

1. Clone or download this repo.
2. Run the script for your shell:

**Fish:**
```fish
chmod +x ./build_vkbasalt_native_optimized.fish
./build_vkbasalt_native_optimized.fish
```
**Bash:**
```bash
chmod +x ./build_vkbasalt_native_optimized.sh
./build_vkbasalt_native_optimized.sh
```

*Note: If you try to use a custom effect (like `crystalclear`) without installing this fork properly, the game will halt Vulkan and fail to launch.*
*If you followed the instructions but crash anyways, you might have multiple previous installations in different directories! You can remove (most) of those with:*

**Bash:**
```bash
VK_LOADER_DEBUG=all vulkaninfo 2>&1 | grep -i basalt

# Remove the JSON manifests
sudo rm -f /usr/share/vulkan/implicit_layer.d/vkBasalt.json
sudo rm -f /usr/local/share/vulkan/implicit_layer.d/vkBasalt.json

# Remove the old .so libraries (some common Linux lib paths)
sudo rm -f /usr/lib/libvkbasalt.so
sudo rm -f /usr/lib/x86_64-linux-gnu/libvkbasalt.so
sudo rm -f /usr/lib32/libvkbasalt.so
sudo rm -f /usr/local/lib/libvkbasalt.so
sudo rm -f /usr/local/lib/x86_64-linux-gnu/libvkbasalt.so
sudo rm -f /usr/local/lib32/libvkbasalt.so
```


### Manual Build
<details>
<summary>Click to expand manual build instructions</summary>

**Dependencies:** GCC >= 9 (or Clang), X11/Wayland dev files, glslang, SPIR-V Headers, Vulkan Headers.

**64-bit:**
```bash
meson setup --buildtype=release --prefix=/usr builddir
ninja -C builddir install
```
**32-bit:**
```bash
ASFLAGS=--32 CFLAGS=-m32 CXXFLAGS=-m32 PKG_CONFIG_PATH=/usr/lib32/pkgconfig meson setup --prefix=/usr --buildtype=release --libdir=lib32 -Dwith_json=false builddir.32
ninja -C builddir.32 install
```
</details>

---

## 🚀 Usage

Enable the layer via environment variables. **No `LD_PRELOAD` required** if installed via the scripts!

*   **Steam:** Add `ENABLE_VKBASALT=1 %command%` to Launch Options.
*   **Lutris:** Go to `Configure` -> `System options` -> `Environment variables`. Add Key: `ENABLE_VKBASALT`, Value: `1`.
*   **Terminal:** `ENABLE_VKBASALT=1 yourgame`

---

## ⚙️ Configuration

The config file (`vkBasalt.conf`) is searched for in this order:
1. `vkBasalt.conf` in the game's working directory (Best for per-game tweaks)
2. `~/.config/vkBasalt/vkBasalt.conf`
3. `~/.local/share/vkBasalt/vkBasalt.conf`
4. `/etc/vkBasalt/vkBasalt.conf` or `/usr/share/vkBasalt/vkBasalt.conf`

### Effect Chains
```ini
# My daily driver, multi-pass (Left to Right). Note: I touched this repo at all just to get this shader in and SMAA should run faster than the default repo's by a bit.
effects = smaa:crystalclear

# Alternative: 5-tap RCAS + Clarity (Great for TAA cleanup, faster on low-end cards, no AA)
effects = clarityrcas

# Alternative 2 : Enable integrated FXAA with crystalclearEnableAA = 1 this'll give you decent FXAA (a bit better, more selective than Nvidia's highest preset) at a low cost compared to SMAA or effects= fxaa:crystalclear
effects = crystalclear
```

### CrystalClear Configuration
```ini
# CrystalClear: All-in-one AA, Macro/Micro Contrast, and Film Grain to combat TAA and bland visuals.
# -- Core & AA Toggles --
# EnableAA: Built-in FXAA. Reuses shader data for performance. Use instead of standalone FXAA, off by default in favor of effects=smaa:crystalclear
crystalclearEnableAA = 0
# FxaaOnlyMode: Debug toggle. Bypasses other effects to only apply FXAA.
crystalclearFxaaOnlyMode = 0
# EnableRGBEdgeDetection: Detects color edges, not just luma for AA.
crystalclearEnableRGBEdgeDetection = 1
# -- Macro Contrast (Clarity) --
# Radius/Offset: Measurement distance for local contrast. Higher = wider, more cinematic pop.
crystalclearBilateralRadius = 2.0
crystalclearBilateralOffset = 1.5
# SharpStrength: Intensity of macro contrast. (1.0 standard, up to 5.0+).
crystalclearSharpStrength = 3.0
# BlendMode: 0=Soft Light (smooth), 1=Overlay (pop), 2=Hard Light, 3=Multiply, 4=Vivid, 5=Linear Light, 6=Addition.
crystalclearBlendMode = 5
# BlendIf Dark/Light: Mid-tone targeting (0-255). Excludes pure shadows/highlights.
crystalclearBlendIfDark = 40
crystalclearBlendIfLight = 220
# EdgeThresh Low/High: Bilateral filter bounds. Low=blur start, High=blur cross (pop).
crystalclearEdgeThreshLow = 0.05
crystalclearEdgeThreshHigh = 0.35
# TextureProtection: Suppresses clarity on micro-textures (gravel, skin) to prevent crunch. (0.0=off, 0.5=balanced, 1.0=max).
crystalclearClarityTextureProtection = 0.5
# -- Micro Contrast (CAS) --
# CasSharpness: Internal CAS curve. (0.0=less sharp, 1.0=max).
crystalclearCasSharpness = 0.8
# CasStrength: Master CAS multiplier. (1.0 standard, up to 5.0).
crystalclearCasStrength = 3.0
# -- FXAA Knobs --
# EdgeThreshold: Min contrast for edge detection. Lower=more AA/blur, Higher=sharper/more aliasing.
crystalclearFxaaEdgeThreshold = 0.05
# EdgeThresholdMin: Dark scene floor. Prevents AA from blurring noise in deep shadows.
crystalclearFxaaEdgeThresholdMin = 0.0312
# SubpixAmount: Subpixel smoothing (thin wires/hair). 1.0=soft, 0.5=sharp, 0.0=off.
crystalclearFxaaSubpixAmount = 1.0
# SearchScale: Edge walk step size. Keep at 1.0.
crystalclearFxaaSearchScale = 1.0
# HardEdgeThreshold: Min perpendicular contrast to start edge walk. Prevents artifacts on soft gradients.
crystalclearFxaaHardEdgeThreshold = 0.08
# -- Perceptual Film Grain --
# Uses 4-layer masking (Luma, Texture, Edge, Sharpening awareness) for organic, non-distracting grain.
crystalclearEnableFilmGrain = 1
# Strength: Overall intensity.
crystalclearFilmGrainStrength = 1.0
# Minimum: Floor amount applied regardless of masks.
crystalclearFilmGrainMinimum = 0
# FineGrainWeight: 1:1 pixel resolution, updates every frame (sharp, high-ISO digital feel).
crystalclearFineGrainWeight = 0.4
# CoarseGrainWeight: 1/4th resolution, updates every 2 frames (heavy, cinematic 35mm clumps).
crystalclearCoarseGrainWeight = 0.8
# -- Dithering & Debug --
crystalclearEnableDithering = 1
# Debug Overlays: Red=FXAA, Green=CAS, Blue=Clarity, Cyan=Grain Mask.
crystalclearEnableDebugAA = 0
crystalclearEnableDebugCAS = 0
crystalclearEnableDebugClarity = 0
crystalclearEnableDebugGrain = 0
```

### ClarityRCAS & Clarity Configuration
```ini
# Clarity RCAS (Standalone): 5-tap RCAS + Clarity (Lighter than CrystalClear) - Refer to crystalclear's descriptions above.
# Range: 0.0 to 2.0.
clarityRcasSharpness = 0.8
# Range: 0.0 to 5.0. >2.0 may cause crunchy artifacts.
clarityRcasStrength = 3.0
# Range: 0 to 255 - lower limit
clarityRcasBlendIfDark = 40
# Range: 0 to 255 - higher limit
clarityRcasBlendIfLight = 220
# Range: 0.0 to 1.0.
clarityRcasEdgeThreshLow = 0.05
# Range: 0.0 to 1.0.
clarityRcasEdgeThreshHigh = 0.35
# Range: 0 to 1.
clarityRcasEnableDithering = 1
# Range: 0 to 1.
clarityRcasEnableFilmGrain = 1
# Range: 0.0 to 2.0.
clarityRcasFilmGrainStrength = 1.0
# Range: 0.0 to 2.0.
clarityRcasFilmGrainMinimum = 0
# Range: 0.0 to 1.0.
clarityRcasFineGrainWeight = 0.4
# Range: 0.0 to 1.0.
clarityRcasCoarseGrainWeight = 0.8

# Standard Clarity (9-Tap) Refer to above for descriptions and ranges.
clarityStrength = 1.0
# Radius/distance from center pixel where macro contrast sampling begins.
clarityRadius = 2
clarityOffset = 1.5
clarityEnableDithering = 1
clarityBlendMode = 5
clarityBlendIfDark = 40
clarityBlendIfLight = 220
clarityEdgeThreshLow = 0.05
clarityEdgeThreshHigh = 0.35
```

### Extended SMAA Configuration
```ini
smaaEdgeDetection = color             # 'color' catches macro edges better than 'luma'
smaaThreshold = 0.05                  # Ultra sensitivity
smaaMaxSearchSteps = 32
smaaMaxSearchStepsDiag = 16
smaaCornerRounding = 0                # 0 preserves razor-sharp geometric corners
```

---

## 🎨 ReShade FX Support

You can run standard ReShade FX shaders (single-technique). You've to place them in your config as such, and modify the .fx file contents to change preset. Same behavior as vkBasalt.
```ini
effects = colorfulness:denoise
colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders
```

---

## 🎮 Input & Debugging

*   **Toggle Effects:** Press `Home` (Configurable). Works on X11 and Wayland.
*   **Hot-Reload Config:** Press `End` (Configurable). Reloads `vkBasalt.conf` in real-time without restarting the game.
*   **Logging:** Set `VKBASALT_LOG_LEVEL=debug` (trace, debug, info, warn, error, none). Output goes to stderr or `VKBASALT_LOG_FILE="vkBasalt.log"`.

---

## ❓ FAQ
#### Why is it called vkBasalt-reloaded?
it's a joke: revived and loaded with new features and fixes, therefore reloaded!
#### Why was it called vkBasalt?
It's a joke: vulkan post processing &#8594; after vulcan &#8594; basalt
#### Does vkBasalt work with dxvk and vkd3d?
Yes.
#### Will vkBasalt get me banned?
Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies.
#### Will there be a openGl version?
No. I don't know anything about openGl and I don't want to either. Also openGl has no layer system like vulkan.
#### Will there be a GUI in the future?
Soon TM. For now alt tab and hot reload ftw.
#### So is vkBasalt just a reshade port for linux?
Not really, most of the code was written from scratch. vkBasalt directly uses reshade source code for the shader compiler (thanks [@crosire](https://github.com/crosire)), but that's about it.
#### Does every reshade shader work?
No. Shaders that need multiple techniques do not work, there might still be problems with stencil and blending and depth buffer access isn't ready yet.
#### You said that "depth buffer access isn't ready yet", what does this mean?
There is a wip version that you can enable with `depthCapture = on`. It will lead to many problems especially on non nvidia hardware. Also the selected depth buffer isn't always the one you would want.
#### Is there a way to change settings for reshade shaders?
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way is to simply edit the shader file.
