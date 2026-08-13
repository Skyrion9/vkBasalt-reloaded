# vkBasalt-reloaded

vkBasalt-reloaded builds on the legacy of vkBasalt to bring you new shaders and layer-level optimizations.

**Why this fork?** This fork aims to bring wayland toggle support, increase performance and curate a collection of highly optimized, native GLSL shaders that drastically outperform ReShade. For example, running `Clarity.fx` + `CAS` through ReShade costs me ~20 Watts and at times requires upscaling. Running the native `CrystalClear` and `ClarityRCAS` shaders in vkBasalt costs just 5-10 Watts at 4K. Besides we don't have to manage wine prefixes or install per-game like reshade requires. This efficiency focused approach is also friendly to handhelds. (Steam Deck, ROG Ally, etc.).

Contributions, PRs, and new shader ports are highly welcome!

Note that maintaining Reshade part of vkbasalt is on the backburner as it's a catch-up game and I'd rather get a stable codebase before increasing the scope. Such few reshade shaders work with vkbasalt to begin with anyways it might just be easier to port them over lol.

The priorities for this fork:
1. Easier to maintan performance focused codebase with highly configurable optimized shaders. ✅ Done and always improving - There are pipeline improvements all over. Expect lower overhead compared to upstream.
2. Implementing a proper Dear ImGui UI to allow per-game configuration in a stable manner. ✅ Done!
3. Before/after screenshot comparison with multi-format export (PNG, JPEG, BMP, TGA, HDR). ✅ Done!
4. Increase the selection of shaders by adding unique open source shaders from known repositories. Suggestions are welcome! (Check out CrystalClear, really it's awesome!)
5. Minimize maintenance cost of adding new shaders. ✅ Done - The method described in [Configuration](CONFIGURATION.md) allows you to simply expose the shaders' parameters to ImGui which handles the UI integration - you're doing almost no extra work as compared to adding a shader to upstream vkBasalt.
6. Compute shader support, as this could increase SMAA performance (Arguably the best AA method in PostFX) by a good margin and allows running a wider array of shaders.

I want to avoid playing catch up with reshade, at least until the above priorities are thoroughly complete. It's a difficult task with how fast reshade is updating. Occasionally reshade might break too as I don't actively test it. However contributions are welcome in this aspect regardless, or issue reports if an effect that worked in original vkBasalt is broken here.


---

## 🌟 Custom Shaders & Engine Enhancements

### Quality/Performance Optimized Shaders
*   **`crystalclear`**: The gem of this repo (and the reason I even work on this) Singlepass jack of all trades shader. Combines AA (off default), CAS, Macro Contrast that brings out detail in midtones without crushing blacks/whites. (Clarity-inspired), multi-scale procedural film grain via UBO, various TAA and other artifact clearing FX that intelligently interact with each other. Edge detection, contrast, UI/Text texture/gradient heuristics all turn this into the perfect shader for minimal artifacting while allowing you to tune 41+ configuration options through the overlay UI to your liking. What's more, it's a **single, highly optimized pass**. and **full HDR (scRGB/HDR10) support with local adaptation scaling**. Basically, is a suite of highly optimized shaders in one package. Carefully tuned with 6 presets to choose from.

How CrystalClear was created:
I wanted to run CAS and other sharpeners in a single shader, because stacking sharpeners is almost never a good idea UNLESS the algorithms are aware of what each sharpener is doing so we don't end up introducing artifacts. It'd also save performance over running multiple effects back to back. Then as I optimized and added more features it evolved into this feature rich & highly optimized neat shader.

1. There isn't a single unnecessary calculation done, if you disable a feature, it's really off.
2. If we fetch pixels for effect A (e.g. CAS) it can be re-used for another effect without fetching again, massive gains over running them back-to-back.
3. 'If I already have data on X amount of pixels already, what effects can I create with these that can run in a single pass?' was the design principle. I like to make the most out of what's available.



*   **`clarityrcas`**: Another optimized singlepass shader. 5-tap anchored local contrast enhancement hybridized with AMD's FSR RCAS. Uses Gaussian bilateral weights, symmetrical clamping, and band-pass protection for zero-halo micro-sharpening. Ideal for cleaning up smudgy TAA or Frame Generation artifacts for lower end systems. This is a lite version of CrystalClear essentially, but since we fetch only 5 + shaped pixels instead of the 3x3 grid, it's more limited so you should avoid this unless you're running integrated GPU or low range. **Now features full HDR support.**
*   **`clarity`**: An optimized single-pass 9-tap cross-convolution local contrast enhancement. Inspired by Reshade's Clarity.fx, this lightweight, singlepass version delivers macro contrast with a fraction of the processing cost. **Now features full HDR support.**

### Upgrades/New features over Upstream
*   **In-game Overlay (imGui)**: Press `Home` to bring up the in-game UI to configure:
- Shader parameters just like in Reshade, your changes are reflected in real-time and you may use the revert button if you messed up.
- You can export/import presets as well as modify the theme of the UI, snap left/right side of the screen!
- Transparency and UI/text scaling to accomodate different configurations.
- You can drag and snap the UI around.
- Note that clicks passthrough the ImGui into the game, this is a known Wayland limitation not a design oversight or bug on our side.

*   **Per-game configuration, presets**: The configuration sticks even if you move games around, tracks steam games by AppIDs to intelligently match.
*   **Hot-Reload Support**: Press `End` to reload config in real-time. Note that the new "toggle effect" key is `Insert` as `Home` is the Overlay button now.

You'll never have to reboot a game to configure vkBasalt-reloaded. Because we uh reloaded it..

*   **HDR Color Space Support**: Native detection of scRGB linear and HDR10 (ST.2084) swapchains. Automatically scales algorithm thresholds via local adaptation luminance and preserves unbounded color ranges for `crystalclear`, `clarity`, and `clarityrcas`, preventing the "washed out" look or math inversions in bright highlights.
*   **Synchronization Overhaul**: Pipeline barriers and access masks slimmed down instead of using all-catching parameters we only fetch and deliver what is needed, increasing efficiency and reducing performance cost. Reducing GPU stalls and bandwidth usage. Basically we're squeezing every drop of performance we can get!

*   **Graphics pipeline caching**: Instead of creating the pipeline from scratch everytime you initialize vkBasalt, we, you guessed it - reload it - from the pipeline cache. We care about the environment here! It's automatically invalidated with GPU/driver version changes too!

*   **Wayland Support**: Native Wayland input for the Overlay, toggle/reload hotkeys. Both mouse and keyboard-optimized overlay interaction. KDE Plasma, GNOME, and Qt/GDK scale detection for HiDPI displays. Relative pointer protocol support for FPS games with pointer lock (cursor works regardless of its existence in-game).
*   **X11/XWayland Support**: Surface interception for X11 pointer tracking, enabling overlay mouse input under XWayland compositors.
*   **Tile-Based GPU Optimizations**: Implemented `VK_ATTACHMENT_LOAD_OP_DONT_CARE` across single-pass effects to maximize mobile/handheld/lowend GPU performance, with explicit clear overrides for multi-pass effects like SMAA.
*   **Temporal UBO Architecture**: Added Vulkan Uniform Buffer Object support for per-frame temporal data for e.g. truly randomized temporal film grain.
🎛️ In-Game Overlay
Press `Home` (configurable) to open the vkBasalt-reloaded configuration overlay.
### Tabs

| Tab | Contents |
| --- | --- |
| **Shaders** | Effect list, searchable parameter controls (sliders, checkboxes, combos), categorized by function. Live preview, Save & Apply / Revert buttons. |
| **Settings** | Cursor scale, UI scale, font scale, keybind editor with conflict resolution, screenshot settings (format, quality, directory, before/after), behavior toggles. |
| **Presets** | Save current parameters as named presets. Load/Delete existing presets. |
| **Style** | Background/accent/text color pickers, opacity and rounding sliders, reset to default. |

### Navigation

| Input | Action |
| :--- | :--- |
| `Mouse` | Full UI navigation supported (click, drag, scroll, tab switching) |
| Drag left / right | Adjust sliders/numeric fields incrementally |
| `Tab` / `Arrows` | Navigate widgets |
| `Enter` | Activate / Edit (click a drag field to type exact values) |
| `Left` / `Right` | Adjust focused value |
| `Space` | Toggle checkbox |
| `Shift` + `Left` / `Right` | Cycle tabs |
| `/` | Focus search box |
| `Esc` | Close overlay |

Window Behavior
- Draggable with edge-snapping: drag to the left or right edge and release to snap. Blue highlight zones appear while dragging.
- Full-height, resizable width. Width and snap side are persisted across sessions.
- Unsaved changes warning on close.
## 🛠️ Building & Installation
**Dependencies:** GCC >= 9 (or Clang), X11/Wayland dev files, glslang, SPIR-V Headers, Vulkan Headers, xkbcommon, spirv-tools (spirv-opt)

### Automated Build Scripts (Recommended)
These scripts automatically apply `-march=native`, ThinLTO, O3, C++20, the `mold` linker, `ccache` support, and assertion stripping. The meson build system also auto-probes your local `glslang` to target the highest supported Vulkan/SPIR-V version, ensuring seamless compilation on both bleeding-edge and LTS distributions. They will prompt for `sudo` to install system-wide and patch the Vulkan manifest for native Proton support.
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

Enable the layer via environment variables.
Note: This was changed from Vkbasalt's default `ENABLE_VKBASALT=1` as Cachy-Proton bundles the old conflicting VKbasalt which lacks our optimizations and custom effects, therefore failing to launch.
This was necessary to seperate the two without workarounds.
*   **Steam:** Add `ENABLE_VKBASALT_RELOADED=1 %command%` to Launch Options. You might have to "force compatibility" a specific Proton version to see launch options.
*   **Lutris:** Go to `Configure` -> `System options` -> `Environment variables`. Add Key: `ENABLE_VKBASALT_RELOADED`, Value: `1`.
*   **Terminal:** `ENABLE_VKBASALT_RELOADED=1 yourgame`

---

## ⚙️ Configuration
See **[Configuration](CONFIGURATION.md)**

Config files are stored in `~/.config/vkBasalt-reloaded/`:

```
~/.config/vkBasalt-reloaded/
├── vkBasalt-reloaded.conf            # Global defaults (all games)
├── games/
│   ├── steam_2357570_Overwatch.conf  # Per-game overrides (auto-detected)
│   └── MyGame_a1b2c3d4.conf          # Non-steam games match by location, binary and md5 hash
└── presets/
    ├── colorful.conf                 # presets you or others exported you can import from!
    └── cleansharp.conf
```

Config Resolution Order
```
Per-game config (highest priority)
Global config
Effect built-in defaults (lowest priority)
Per-Game Config Naming
Format: `<identifier>.conf`
Steam games: `steam_<appid>_<GameName>.conf` (e.g. `steam_2357570_Overwatch.conf`)
Non-Steam: `<exename>_<md5hash>.conf` (e.g. `Overwatch_938c2cc0.conf`)
```
The MD5 hash is computed from the full executable path, ensuring unique configs even for games with identical names.

### Keybinds (Default)

| Action | Default Key | Config Key |
| :--- | :--- | :--- |
| Toggle effects on/off | `Insert` | `toggleKey` |
| Hot-reload config from disk | `End` | `reloadConfigKey` |
| Open/close overlay | `Home` | `overlayToggleKey` |
| Take screenshot | `Delete` | `screenshotKey` |

All keybinds are rebindable in the overlay's Settings tab, or directly in the config file.


HiDPI / Display Scaling
The overlay auto-detects your display scale from multiple sources (in priority order):
```
`cursorScale` config value (manual override)
KDE Plasma config (`~/.config/kwinrc` Scale=)
Qt/GDK environment variables (`QT_SCALE_FACTOR`, `GDK_SCALE`)
`wl_output` integer scale (Wayland)
Xft.dpi (X11)
```
You can override each independently:
| Config Key | Purpose | Default |
| --- | --- | --- |
| `cursorScale` | Mouse coordinate mapping only. Change only if pointer is misbehaving. | 0 (auto) |
| `uiScale` | Widget/padding/spacing size. Does NOT affect mouse. | 0 (auto) |
| `fontScale` | Additional font size multiplier on top of uiScale. | 0 (=1.0) |

All shader parameters are configurable via the overlay or config file. Only touch the config file if you can't reach the overlay somehow. You can always delete them if you need them remade upon game launch.

🎨 ReShade FX Support
You can run standard ReShade FX shaders (single-technique). You've to place them in your config as such, and modify the .fx file contents to change preset. Same behavior as vkBasalt.
effects = colorfulness:denoise
colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders

🎮 Input & Debugging
You may rebind these defaults in the ImGui UI
Toggle Effects: Press `Insert` (Configurable). Works on X11 and Wayland.
Hot-Reload Config: Press `End` (Configurable). Reloads config in real-time without restarting the game.
Open Overlay: Press `Home` (Configurable). Full ImGui configuration UI.
Take Screenshot: Press `Delete` (Configurable). Captures the current frame as PNG/JPEG/BMP/TGA/HDR. Optionally saves before/after comparison.

Logging: Set env flag `VKBASALT_LOG_LEVEL=debug` (trace, debug, info, warn, error, none). Output goes to stderr or `VKBASALT_LOG_FILE="vkBasalt.log"`.
## ❓ FAQ
#### Why are you working on this Nth fork of vkBasalt?
I've checked out all the forks, they're either degraded in performance or has various other issues, none of them fit the bill for me on "high performance, with GUI, maintainable codebase" and most of all they don't have CrystalClear shader, which is what drove me to fork to begin with.
#### I can't code, can I contribute?
Yes, donations allow me to put more time into this project for long term maintenance and feature enhancements. Any amount is welcome, you may donate at:

USDT, network: TRC-20 / Tron
`TK6hN7BFdzQWceBdR8srbnYb8VxAa7TMJ7`

USDT, network: ETH/ERC-20
`0x71bcd521b22d49a72437ee44f942b6ffb093f77a`

USDT, network: SOL
`7BLMiGh9ExhiZCL8c7P7hPv5sSotU7hRsbe5vokVKrqS`

#### Why is it called vkBasalt-reloaded?
it's a joke: revived and loaded with new features and fixes, supports hot-reloading and live configuration therefore reloaded!
#### Why was it called vkBasalt?
It's a joke: vulkan post processing &#8594; after vulcan &#8594; basalt
#### Does vkBasalt work with dxvk and vkd3d?
Yes.
#### Will vkBasalt get me banned?
Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies.
#### Will there be a openGl version?
No. I don't know anything about openGl and I don't want to either. Also openGl has no layer system like vulkan.
#### So is vkBasalt just a reshade port for linux?
Not really, most of the code was written from scratch. vkBasalt directly uses reshade source code for the shader compiler (thanks [@crosire](https://github.com/crosire)), but that's about it.
#### Does every reshade shader work?
No. Shaders that need multiple techniques do not work, there might still be problems with stencil and blending and depth buffer access isn't ready yet.
#### You said that "depth buffer access isn't ready yet", what does this mean?
There is a wip version that you can enable with `depthCapture = on`. It will lead to many problems especially on non nvidia hardware. Also the selected depth buffer isn't always the one you would want.
#### Is there a way to change settings for reshade shaders?
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way is to simply edit the shader file.
