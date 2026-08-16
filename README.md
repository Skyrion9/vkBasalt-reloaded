# vkBasalt-reloaded

Builds on the legacy of vkBasalt to bring you a full Dear ImGui configuration UI, highly optimized native GLSL shaders, and layer-level Vulkan optimizations.

**Why this fork?**
As of writing this, this is the best vkBasalt fork out there brought to you by an optimization obsessed developer.

Running `Clarity.fx` + `CAS` through ReShade costs me ~20 Watts and often requires upscaling. Running my native `CrystalClear` and `ClarityRCAS` shaders in vkBasalt-reloaded costs just 5-10 Watts at 4K. 
- Per-game config without needing to mess with text editors outside of the game. 
- Drastically lower overhead. Efficiency first approach makes it perfect for handhelds (Steam Deck, ROG Ally) and high refreshrate 4K gaming.

Contributions, PRs, and new shader ports are highly welcome!

---

## 🚀 Performance & Architecture Overhauls

*   **Dynamic Pipeline Synchronization:** Replaced 'safe' 'pipeline barriers with precise, dynamically chosen chain aware stage masks and access flags. We only stall the GPU when needed.
*   **Graphics Pipeline Caching:** Pipelines are cached to disk and reloaded on startup. No more compiling shaders from scratch every time you launch a game. Automatically invalidates on GPU/driver changes.
*   **Tile-Based GPU Optimizations:** Implemented `VK_ATTACHMENT_LOAD_OP_DONT_CARE` across single-pass effects to maximize mobile/handheld/low-end GPU performance.
*   **Zero  Cost Bypass:** Toggling effects off via the UI or hotkey completely skips effect processing, UBO updates, and mutex acquisitions. Zero overhead when you want raw game performance.
*   **Temporal UBO Architecture:** Added Vulkan Uniform Buffer Object support for per-frame temporal data, enabling truly randomized temporal film grain without running complex GPU calculations.
*   **Feature rich, performance richer:** All features are carefully tuned and filtered for performance regressions. There are too many optimizations to list but expect immesurable overhead.
*   **Targeted compiler optimizations:** Utilizes spirv-opt, flags like thinLTO and -march=native to squeeze every drop of runtime performance at a reduced binary size. (Only 3.9 MB packing all these shaders and ImGui)
*   **Optimized single pass master shader:** CrystalClear (CC) is the jack of all trades (even includes AA lol) shader that packs above its punch. 
- What's more, if you disable certain effects in CC, they're truly turned off and consume no extra cycles. 
- The cost of enabling effects within CC is minor, as we reuse calculations and datapoints whenever possible.

## 🎛️ Dear ImGui Overlay & Configuration

*   **Full In-Game UI:** Press `Home` to open a fully featured ImGui overlay. Tweak 60+ shader parameters in real-time, manage presets, and customize the UI theme. (Note: Clicks pass through to the game on Wayland due to compositor limitations, not a bug on our side).
*   **Smart Per-Game Configs:** Configs track Steam games by AppID and non-Steam games by executable path/MD5 hash. Move your games around; your settings follow them.
*   **Preset System:** Save, load, and delete named presets directly from the UI. Export and share your `.conf` files with friends.
*   **Hot-Reload & Live Preview:** Press `End` to reload configs from disk instantly. Slider adjustments in the UI trigger optimized live previews without stalling the render thread.
*   **Clean Screenshots:** Snap overlay-free screenshots in PNG, JPEG, BMP, TGA, or HDR formats, with optional before/after comparison exports.

## 🌟 Custom Shaders

*   **`crystalclear`**: The crown jewel. A single-pass, jack-of-all-trades shader combining CAS, macro contrast, multi-scale procedural film grain, and intelligent artifact clearing. It reuses pixel fetches across effects to eliminate redundant memory reads. Features 60+ configurable options, 8+ curated presets, and full HDR (scRGB/HDR10) support. If a feature is disabled in the UI, the performance cost is literally zero.
*   **`clarityrcas`**: A lightweight 5-tap anchored local contrast enhancement hybridized with AMD's FSR RCAS. Uses Gaussian bilateral weights for zero-halo micro-sharpening. Ideal for cleaning up smudgy TAA or Frame Generation artifacts on integrated GPUs. Full HDR support.
*   **`clarity`**: An optimized single-pass 9-tap cross-convolution local contrast enhancement. Delivers macro contrast at a fraction of the processing cost of it's inspiration: ReShade's `Clarity.fx`. Full HDR support.

## 🖥️ Display & Input

*   **Native Wayland & X11/XWayland:** Full mouse and keyboard support, edge-snapping UI, and HiDPI scaling detection (KDE, GNOME, Qt/GDK, wl_output).
*   **Relative Pointer Protocol:** Cursor works seamlessly in FPS games with pointer lock on Wayland.
*   **HDR Color Space Support:** Native detection of scRGB linear and HDR10 (ST.2084) swapchains. Automatically scales algorithm thresholds via local adaptation luminance to prevent washed-out highlights.

---

## 🎛️ In-Game Overlay Navigation

Press `Home` (configurable) to open the overlay.

| Tab | Contents |
| --- | --- |
| **Shaders** | Effect list, searchable parameter controls, categorized by function. Live preview, Save/Revert, and right-click "Reset to Default". |
| **Settings** | Cursor/UI scale, keybind editor, screenshot settings, behavior toggles. |
| **Presets** | Save current parameters as named presets. Load/Delete existing presets. |
| **Style** | Background/accent/text color pickers, opacity and rounding sliders. |

| Input | Action |
| :--- | :--- |
| `Mouse` | Full UI navigation (click, drag, scroll, tab switching) |
| Drag left / right | Adjust sliders/numeric fields incrementally |
| `Tab` / `Arrows` | Navigate widgets |
| `Enter` | Activate / Edit (click a drag field to type exact values) |
| `Space` | Toggle checkbox |
| `Shift` + `Left` / `Right` | Cycle tabs |
| `/` | Focus search box |
| `Esc` | Close overlay |

**Window Behavior:** Draggable with edge-snapping (drag to the left or right edge and release). Full-height, resizable width. Width and snap side are persisted across sessions.

---

## 🛠️ Building & Installation

**Dependencies:** GCC >= 9 (or Clang), X11/Wayland dev files, glslang, SPIR-V Headers, Vulkan Headers, xkbcommon, spirv-tools (spirv-opt)

### Automated Build Scripts (Recommended)
These scripts automatically apply `-march=native`, ThinLTO, O3, C++20, the `mold` linker, `ccache` support, and assertion stripping. The meson build system auto-probes your local `glslang` to target the highest supported Vulkan/SPIR-V version. They will prompt for `sudo` to install system-wide and patch the Vulkan manifest for native Proton support.

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

Enable the layer via environment variables, press `Home` in-game.
*Note: This was changed from upstream's `ENABLE_VKBASALT=1` because Cachy-Proton bundles the old conflicting vkBasalt which lacks our optimizations and custom effects, causing launch failures. This separates the two without workarounds.*

*   **Steam:** Add `ENABLE_VKBASALT_RELOADED=1 %command%` to Launch Options.
*   **Lutris:** Go to `Configure` -> `System options` -> `Environment variables`. Add Key: `ENABLE_VKBASALT_RELOADED`, Value: `1`.
*   **Terminal:** `ENABLE_VKBASALT_RELOADED=1 yourgame`

---

## ⚙️ Configuration

See **[Configuration](CONFIGURATION.md)** for deep dives. Config files are stored in `~/.config/vkBasalt-reloaded/`:

```text
~/.config/vkBasalt-reloaded/
├── vkBasalt-reloaded.conf            # Global defaults (all games)
├── games/
│   ├── steam_2357570_Overwatch.conf  # Per-game overrides (auto-detected)
│   └── MyGame_a1b2c3d4.conf          # Non-steam games match by location, binary and md5 hash
└── presets/
    ├── colorful.conf                 # Exported/imported presets
    └── cleansharp.conf
```

**Config Resolution Order:**
1. Per-game config (highest priority)
2. Global config
3. Effect built-in defaults (lowest priority)

### Keybinds (Default)

| Action | Default Key | Config Key |
| :--- | :--- | :--- |
| Toggle effects on/off | `Insert` | `toggleKey` |
| Hot-reload config from disk | `End` | `reloadConfigKey` |
| Open/close overlay | `Home` | `overlayToggleKey` |
| Take screenshot | `Delete` | `screenshotKey` |

All keybinds are rebindable in the overlay's Settings tab.

### HiDPI / Display Scaling
The overlay auto-detects your display scale from multiple sources (KDE `kwinrc`, Qt/GDK env vars, `wl_output`, Xft.dpi).

| Config Key | Purpose | Default |
| --- | --- | --- |
| `cursorScale` | Mouse coordinate mapping only. Change only if pointer is misbehaving. | 0 (auto) |
| `uiScale` | Widget/padding/spacing size. Does NOT affect mouse. | 0 (auto) |
| `fontScale` | Additional font size multiplier on top of uiScale. | 0 (=1.0) |

---

## 🎨 ReShade FX Support

You can run standard ReShade FX shaders (single-technique). Place them in your config as such. Expect same behavior as upstream vkBasalt, although it's largely untested/unmaintained except for compilation-breaking changes.

```ini
effects = colorfulness:denoise
colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders
```

---

## ❓ FAQ
**Something is broken, how can I get support?**
First, log the issue, your system specs, OS and all relevant info like drivers, env flags etc. then create an Issue here on Github.
`Logging: Set env flag `VKBASALT_LOG_LEVEL=debug` (trace, debug, info, warn, error, none). Output goes to stderr or `VKBASALT_LOG_FILE="vkBasalt.log"` `

**Why are you working on this Nth fork of vkBasalt?**
I've checked out all the forks. They're either degraded in performance, lack a GUI, or have maintainability issues. None of them fit the bill for "high performance, with GUI, maintainable codebase", and most importantly, they don't have the CrystalClear shader, which is what drove me to fork to begin with.

**I can't code, can I contribute?**
Yes, donations allow me to put more time into this project for long term maintenance and feature enhancements. Any amount is welcome.
*   USDT (TRC-20 / Tron): 

`TK6hN7BFdzQWceBdR8srbnYb8VxAa7TMJ7`
*   USDT (ETH/ERC-20): 

`0x71bcd521b22d49a72437ee44f942b6ffb093f77a`
*   USDT (SOL): 

`7BLMiGh9ExhiZCL8c7P7hPv5sSotU7hRsbe5vokVKrqS`

**Why is it called vkBasalt-reloaded?**
It's a joke: revived and loaded with new features and fixes, supports hot-reloading and live configuration. Therefore, reloaded!

**Why was it called vkBasalt?**
It's a joke: vulkan post processing &#8594; after vulcan &#8594; basalt.

**Does vkBasalt work with dxvk and vkd3d?**
Yes.

**Will vkBasalt get me banned?**
Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies.

**Will there be an openGl version?**
No. I don't know anything about openGl and I don't want to either. Also openGl has no layer system like vulkan.

**So is vkBasalt just a reshade port for linux?**
Not really, most of the code was written from scratch. vkBasalt directly uses reshade source code for the shader compiler (thanks [@crosire](https://github.com/crosire)), but that's about it.

**Does every reshade shader work?**
No. Shaders that need multiple techniques do not work, there might still be problems with stencil and blending, and depth buffer access isn't fully baked yet.

**You said that "depth buffer access isn't ready yet", what does this mean?**
There is a WIP version that you can enable with `depthCapture = on`. It will lead to many problems especially on non-Nvidia hardware. Also, the selected depth buffer isn't always the one you would want.

**Is there a way to change settings for reshade shaders?**
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way is to simply edit the shader file.
