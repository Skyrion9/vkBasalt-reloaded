## IMPORTANT NOTE: 
The repo has been moved to [vkBasalt-reloaded repo](https://github.com/Skyrion9/vkBasalt-reloaded) the development will continue there. No point holding a fork since vkBasalt's original repo is no longer maintained. It's a direct mirror of this repo with preserved history.

Contributions are welcome as the original repo is seemingly abandoned? Over time I'd like to have a collection of vkBasalt implemented GLSL shaders since this is a highly optimized solution as compared to Reshade. I had to turn down my upscaling to be able to run Clarity.fx through Reshade and it cost me about 20 Watts (60 fps limit) to toggle that + CAS. Now it's just about 4-5 Watts in 4k.

This fork extends the original vkBasalt with push constant support, native Steam/Proton integration, a modernized build system, and heavily optimized custom GLSL shaders. 

* **Push Constant Support:** Reduces repetitive calls like fetching screen size (`textureSize`) on each shader iteration. Instead, `texelSize` is readily available to the shader via push constants, increasing performance in Clarity and ClarityCAS.
* **Native Steam/Proton Integration:** Build scripts automatically patch the Vulkan implicit layer manifest to use absolute paths. This allows the Steam Linux Runtime (Pressure Vessel) to natively map the layer into game containers, completely eliminating the need for `LD_PRELOAD` hacks.
* **Modernized Build Pipeline:** Upgraded to C++20 and C17, utilizing ThinLTO and stripping debug assertions (`b_ndebug=if-release`) for a smaller, cache-efficient binary (>100 KB smaller 3.5 MB to 3.4 MB).
* **Custom Shaders (Clarity & ClarityCAS):** Meticulously optimized, single-pass implementations featuring bilateral edge-stopping, gamma protection, and algebraic reductions. More settings & knobs so you may more granularly control the visual fidelity.


# Custom VKBasalt GLSL ClarityRCAS (Hybrid) Implementation
> * **5-Tap RCAS Cross-Neighborhood:** Uses only 4 cross-neighbor fetches (b, d, f, h) instead of 8, saving 4 texture fetches compared to standard CAS while maintaining quality.
> * **RCAS Clamp for Zero Haloing:** Implements strict delta clamping (`clamp(sharpDelta, mnRGB - e, mxRGB - e)`) that mathematically guarantees zero haloing/clipping on top of the Clarity S-curve.
> * **Unified Single-Pass Hybrid Engine:** Seamlessly merges the macro-contrast enhancement of Clarity with the micro-detail acutance of AMD's FidelityFX CAS into a single, highly optimized render pass.
> * **Bilateral Delta Accumulation:** Replaces traditional blur-and-subtract methods with direct delta weighting. By evaluating luminance differences on the fly, the shader physically prevents wide-radius blurs from crossing high-contrast boundaries, completely eliminating "15-pixel ghosting" and haloing artifacts around UI text and sharp edges.
> * **Extremes Masking & Branchless Choking:** Utilizes a parabolic extremes stopper and branchless SIMD instructions to protect pure blacks and whites from S-curve overshoot, preserving native tone distributions while enhancing mid-tone texture.
> * **Zero Intermediate Framebuffers:** Like the custom Clarity pass, the hybrid engine executes entirely in textureless L1 registers, requiring only 13 texture taps and zero VRAM bandwidth overhead for auxiliary render targets.
> * **Exposed Full 8 Parameter Configuration:** Fully configurable via `vkBasalt.conf`, allowing independent tuning of both the Clarity macro-contrast and RCAS micro-sharpening components.

# Custom VKBasalt GLSL ClarityCAS (Hybrid) Implementation
> * **Unified Single-Pass Hybrid Engine:** Seamlessly merges the macro-contrast enhancement of Clarity with the micro-detail acutance of AMD's FidelityFX CAS into a single, highly optimized render pass.
> * **Bilateral Delta Accumulation:** Replaces traditional blur-and-subtract methods with direct delta weighting. By evaluating luminance differences on the fly, the shader physically prevents wide-radius blurs from crossing high-contrast boundaries, completely eliminating "15-pixel ghosting" and haloing artifacts around UI text and sharp edges.
> * **CAS Zero-Crossing Protection:** Implements a strict mathematical clamp on CAS adaptive weights to prevent inversion and instability on extreme contrast edges, ensuring text and sharp geometry remain pristine without "reprojection" bleeding.
> * **Extremes Masking & Branchless Choking:** Utilizes a parabolic extremes stopper and branchless SIMD instructions to protect pure blacks and whites from S-curve overshoot, preserving native tone distributions while enhancing mid-tone texture.
> * **Zero Intermediate Framebuffers:** Like the custom Clarity pass, the hybrid engine executes entirely in textureless L1 registers, requiring only 17 texture taps and zero VRAM bandwidth overhead for auxiliary render targets.
> * **Exposed Full 8 Parameter Configuration:** Fully configurable via `vkBasalt.conf`, allowing independent tuning of both the Clarity macro-contrast and CAS micro-sharpening components. *(Halo intensity params are invalid for the hybrid shader as they are unnecessary due to the bilateral nature of this implementation).*

# Custom VKBasalt GLSL Clarity Implementation
> * **Integrated Custom Clarity Effect:** Added a native, single-pass GLSL implementation of the local contrast enhancement filter directly into the built-in effects stack.
> * **Axis-Aligned Cross-Convolution:** Replaced high-overhead multi-pass methods from legacy `Clarity.fx` with a branchless, 8-tap horizontal/vertical cross-convolution layout to match axis-aligned game geometry perfectly while maintaining matching image quality.
> * **Zero-Intermediate Texture Allocation:** Completely eliminated the half-resolution auxiliary render targets (`ClarityTex` and `ClarityTex2`) utilized by traditional `Clarity.fx` for downsample and blur passes. Running strictly in a textureless single pass avoids intermediate frame-buffer blits and drastically cuts VRAM bandwidth consumption.
> * **Hardware Bilinear Exploitation:** Utilizes fractional sample strides (`1.5` and `4.5` texels out) to offload interpolation to the GPU's native hardware bilinear filtering units, securing wide-radius blurring footprints with zero extra ALU performance cost.
> * **Scalar Luma Reconstruction:** Abandoned the traditional ReShade method of splitting color vectors into explicit chrominance arrays (`color / luma`). By executing operations entirely through a late-stage single float multiplier (`lumaScale`), the implementation alleviates vector register pressure and maximizes GPU wavefront occupancy.
> * **Branchless Pipeline Flattening:** Swapped the conditional if/else runtime forks found in `Clarity.fx` for a branchless ternary selection pattern. This compiles directly into hardware execution predicates (`CSEL`), completely preventing thread stalls inside the GPU execution warp.
> * **Gamma Shift Resolution:** Properly clamp and respect the original gamma ensuring pixels don't overshoot black or whites in the extremes. 
> NOTE : Unlike the ClarityCAS hydbrid shader, Clarity has to substract intensity from white/black halos and is very prone to causing strong haloing effect. I highly recommend using ClarityCAS or ClarityRCAS instead due to its more intelligent bilateral nature that's free of artifacts.

---

## Building with scripts (Optimized flags)
1. Simply clone, or download as zip and extract this repo.  
2. Run either script below. It will prompt for your `sudo` password to install the library system-wide and automatically patch the Vulkan manifest for native Proton support.
3. Launch your game normally using `ENABLE_VKBASALT=1 %command%` in Steam. **No `LD_PRELOAD` required!** but you can also use that if you prefer. 

Remember if you don't preload or install properly and try to use an effect (Clarity, ClarityCAS, ClarityRCAS) that doesn't exist in the original vkBasalt the game will not launch! This is the same behavior as entering effects = donkeykong in the vkbasalt.conf file as it can't find such effect and halts vulkan.

Both scripts automatically apply `-march=native`, while the Meson build system handles ThinLTO, O3, C++20, and assertion stripping for a performant library.

Open project folder in terminal and run:

### Fish
```fish
chmod +x ./build_vkbasalt_native_optimized.fish
./build_vkbasalt_native_optimized.fish
```

Or

### Bash
```bash
chmod +x ./build_vkbasalt_native_optimized.sh
./build_vkbasalt_native_optimized.sh
```

---

#### If you want to preload per-app instead of installing system-wide:
Not recommended, only do this if you know what you're doing, install system-wide with the scripts instead.
```
LD_PRELOAD="/run/media/USERNAME/Home/vkBasalt/build/src/libvkbasalt.so" ENABLE_VKBASALT=1 %command
```


# vkBasalt
vkBasalt is a Vulkan post processing layer to enhance the visual graphics of games.

Currently, the built-in effects are:
- Contrast Adaptive Sharpening (**Enhanced performance via algebraic reduction, identical Image Quality**)
- Denoised Luma Sharpening
- Fast Approximate Anti-Aliasing
- Enhanced Subpixel Morphological Anti-Aliasing (**Extended SMAA configuration so you can maximize quality or performance!**)
- 3D color LookUp Table
- **Clarity (Optimized Single-Pass Local Contrast Enhancement)**
- **ClarityCAS (Optimized Single-Pass Hybrid Contrast & Sharpening)**
- **ClarityRCAS (Optimized 5-Tap RCAS Hybrid with Zero Haloing)**

It is also possible to use Reshade Fx shaders.

## Disclaimer
This is one of my first projects ever, so expect it to have bugs. Use it at your own risk.

## Building from Source (Manual)

### Dependencies
Before building, you will need:
- GCC >= 9 (or Clang)
- X11 development files
- glslang
- SPIR-V Headers
- Vulkan Headers

### Building

**These instructions use `--prefix=/usr`, which is generally not recommended since vkBasalt will be installed in directories that are meant for the package manager. The alternative is not setting the prefix, it will then be installed in `/usr/local`. But you need to make sure that `ld` finds the library since /usr/local is very likely not in the default path.** 

In general, prefer using distro provided packages (for our shaders you need to use the build scripts provided above instead)

```bash
git clone https://github.com/DadSchoorse/vkBasalt.git
cd vkBasalt
```

#### 64bit

```bash
meson setup --buildtype=release --prefix=/usr builddir
ninja -C builddir install
```
#### 32bit

Make sure that `PKG_CONFIG_PATH=/usr/lib32/pkgconfig` and `--libdir=lib32` are correct for your distro and change them if needed. On Debian based distros you need to replace `lib32` with `lib/i386-linux-gnu`, for example.
```bash
ASFLAGS=--32 CFLAGS=-m32 CXXFLAGS=-m32 PKG_CONFIG_PATH=/usr/lib32/pkgconfig meson setup --prefix=/usr --buildtype=release --libdir=lib32 -Dwith_json=false builddir.32
ninja -C builddir.32 install
```

## Packaging status

[Debian](https://tracker.debian.org/pkg/vkbasalt) `sudo apt install vkbasalt`

[Fedora](https://src.fedoraproject.org/rpms/vkBasalt) `sudo dnf install vkBasalt`

[Void Linux](https://github.com/void-linux/void-packages/blob/master/srcpkgs/vkBasalt/template) `sudo xbps-install vkBasalt`

## Usage
Enable the layer with the environment variable.

### Standard
When using the terminal or an application (.desktop) file, execute:
```ini
ENABLE_VKBASALT=1 yourgame
```

### Lutris
With Lutris, follow these steps below:
1. Right click on a game, and press `configure`.
2. Go to the `System options` tab and scroll down to `Environment variables`.
3. Press on `Add`, and add `ENABLE_VKBASALT` under `Key`, and add `1` under `Value`.

### Steam
With Steam, edit your launch options and add:
```ini
ENABLE_VKBASALT=1 %command% 
```

## Configure

Settings like the CAS sharpening strength can be changed in the config file.
The config file will be searched for in the following locations:
* a file set with the environment variable `VKBASALT_CONFIG_FILE=/path/to/vkBasalt.conf`
* `vkBasalt.conf` in the working directory of the game
* `$XDG_CONFIG_HOME/vkBasalt/vkBasalt.conf` or `~/.config/vkBasalt/vkBasalt.conf` if `XDG_CONFIG_HOME` is not set
* `$XDG_DATA_HOME/vkBasalt/vkBasalt.conf` or `~/.local/share/vkBasalt/vkBasalt.conf` if `XDG_DATA_HOME` is not set
* `/etc/vkBasalt.conf`
* `/etc/vkBasalt/vkBasalt.conf`
* `/usr/share/vkBasalt/vkBasalt.conf`

If you want to make changes for one game only, you can create a file named `vkBasalt.conf` in the working directory of the game and change the values there.

#### Clarity Configuration
Here's how to configure clarity's VKBasalt implementation in `vkBasalt.conf`:

**The newly expanded clarity effect has eight fully configurable parameters parsed natively by the pipeline:**

### 1. `clarityStrength` (default: **1.00**)
* **Purpose**: Controls the amount of contrast enhancement
* **Range**: 0.0 (no effect) to 1.0 (maximum enhancement)
* **Default**: **1.00 (Recommend 0.6 if it's too much for you :) )**

### 2. `clarityRadius` (default: **2**)
* **Purpose**: Controls the base component radius of the local contrast effect in pixels
* **Range**: Recommended 1 - **5**

### 3. `clarityOffset` (default: **1.50**)
* **Purpose**: Fine-tunes the physical sampling step size of the sparse cross-convolution footprint. Paired with native GPU bilinear texture filtering, fractional offsets (e.g., 1.5 and 4.5) stretch the sampling area efficiently across wider pixel zones without introducing cache misses or extra texture fetch instructions.

### 4. `clarityBlendMode` (default: **1**)
* **Purpose**: Changes the mathematical equation used to composite the contrast mask.
* **Supported Operational Constants:**
  * **0: Soft Light** (Highly subtle local variance)
  * **1: Overlay** (Default: Ideal balance using a smooth S-curve blend model to preserve native tone distributions)
  * **2: Hard Light**
  * **3: Multiply**
  * **4: Vivid Light**
  * **5: Linear Light**
  * **6: Addition** (Raw value stack; can lead to clipping if used with extreme radius configurations)

### 5. `clarityBlendIfDark` (default: **40**) & `clarityBlendIfLight` (default: **220**)
* **Purpose**: Hardware mid-tone luminance masking gates. Coordinates with an unweighted average function to isolate the effect from deep crushing blacks (values below BlendIfDark) or clipping stark highlights/skyboxes (values above BlendIfLight). Values scale on a traditional 0-255 depth spectrum.

### 6. `clarityDarkIntensity` (default: **0.16**) & `clarityLightIntensity` (default: **0.0**)
* **Purpose**: Symmetrical halo attenuation parameters.
* **Note on Gamma/Tone:** Features a gamma protection update that keeps pure blacks and whites from S-curve overshoot, and by defaulting to the Overlay blend mode. The intensity parameters cleanly control halo choking without destroying the image's natural gamma roll-off. Pushing these coefficients closer to 1.0 filters out thick black and white rings near extreme contrast thresholds. Recommended ranges 0.1 - 0.3.

This isn't even needed on the hybrid configuration as it's clean and without big halos due to its bilateral heuristics.

#### ClarityCAS (Hybrid) Configuration
Here's how to configure the hybrid ClarityCAS effect in `vkBasalt.conf`:

**The hybrid effect exposes eight fully configurable parameters, combining the best of both Clarity and CAS:**

### 1. `clarityCasStrength` (default: **1.0**)
* **Purpose**: Controls the amount of macro-contrast enhancement (Clarity component).
* **Range**: 0.0 (no effect) to 1.0 (maximum enhancement).

### 2. `clarityCasRadius` (default: **2**) & `clarityCasOffset` (default: **1.5**)
* **Purpose**: Controls the base radius and fractional sampling step size of the local contrast effect.
* **Note**: Lower radii (1 or 2) are recommended to prevent macro-halos around UI elements.

### 3. `clarityCasBlendMode` (default: **1**)
* **Purpose**: Changes the mathematical equation used to composite the contrast mask.
* **Recommendation**: **0 (Soft Light)** is highly recommended for the hybrid shader to prevent harsh contrast spikes on bright text compared to Overlay.

### 4. `clarityCasBlendIfDark` (default: **40**) & `clarityCasBlendIfLight` (default: **220**)
* **Purpose**: Mid-tone luminance masking gates to isolate the effect from deep blacks and stark highlights.

### 5. `clarityCasCasSharpness` (default: **1.0**) & `clarityCasCasStrength` (default: **1.0**)
* **Purpose**: Controls the micro-detail acutance (CAS component). 
* **Note**: `0.4` is AMD's official mathematical sweet spot for CAS sharpness. `0.7` strength provides beautiful micro-detail without creating edge ringing.


#### ClarityRCAS (Hybrid) Configuration
Robust CAS is what AMD uses in FSR to minimize artifacting as the upscaling process introduces artifacts. "Normal" CAS introduces micro-contrast which gives a perception of higher detail or sharpness. As a side effect it boosts the visibility of any artifact on screen. RCAS is more selective in this and clams this contrast boost to prevent color crushing. For purists this is higher quality.

For our purposes of using with Clarity, I prefer this method as it was engineered to minimize artifact-boosting where halos, ringing artifacts etc. might be exaggerated. Although our bilateral clarity approach minimizes artifacts, this is the preffered method when applying multiple sharpness filters and expecting a not-so-clear input such as smudgy TAA or frame generation artifacts.

**The ClarityRCAS effect uses a 5-tap RCAS cross-neighborhood with strict delta clamping for zero haloing:**

Shares the same variables as ClarityCAS except these two below.

### `clarityRcasCasSharpness` (default: **0.4**) & `clarityRcasCasStrength` (default: **1.0**)
* **Purpose**: Controls the micro-detail acutance (RCAS component). Similar to above but minimal artifacting, clean  sharp.

#### Extended SMAA Configuration
This fork expands the standard vkBasalt SMAA implementation with granular control over diagonal edge detection and quality presets, allowing you to fine-tune the balance between performance and anti-aliasing quality.

### 1. `smaaDisableDiagDetection` (default: **0**)
* **Purpose**: Controls whether diagonal edge detection is enabled.
* **Values**:
  * **0**: Enabled (Default). Provides better quality for diagonal edges but costs slightly more performance.
  * **1**: Disabled. Provides better performance, similar to the `SMAA_PRESET_MEDIUM` behavior.

### 2. `smaaPreset` (Optional)
* **Purpose**: Allows you to use predefined quality presets **instead** of configuring individual SMAA settings. 
* **Note**: Presets and individual settings are mutually exclusive. If a preset is specified, individual settings (like `smaaThreshold`, `smaaMaxSearchSteps`, etc.) are ignored.
* **Available Presets**:
  * **`low`** (~60% quality): `smaaThreshold = 0.15`, `smaaMaxSearchSteps = 4`, `smaaMaxSearchStepsDiag = 0`, `smaaCornerRounding = 25`
  * **`medium`** (~80% quality): `smaaThreshold = 0.10`, `smaaMaxSearchSteps = 8`, `smaaMaxSearchStepsDiag = 0`, `smaaCornerRounding = 25`
  * **`high`** (~95% quality): `smaaThreshold = 0.10`, `smaaMaxSearchSteps = 16`, `smaaMaxSearchStepsDiag = 8`, `smaaCornerRounding = 25`
  * **`ultra`** (~99% quality): `smaaThreshold = 0.05`, `smaaMaxSearchSteps = 32`, `smaaMaxSearchStepsDiag = 16`, `smaaCornerRounding = 25`

*Perceptually there's very little difference between medium and high. Ultra is mostly useless and heavy.*

## How to Enable and Configure

In your `~/.config/vkBasalt/vkBasalt.conf` file (or wherever the final config is installed), modify:

```ini
# Enable standard clarity effect:
effects = clarity

# OR Enable the hybrid effect (ClarityCAS):
effects = claritycas

# OR Enable the RCAS hybrid effect (ClarityRCAS - optimized, zero haloing):
effects = clarityrcas

# Or combine multiple effects (they run left to right):
# effects = smaa:clarityrcas
# Note: 'effects = clarity:cas' runs 2 separate effects. 'effects = clarityrcas' runs the single hybrid effect. 
# (Highly Recommended: running them separately is 4x+ more expensive and is an artifact fest without hybrid considerations.)

# Adjust the standard clarity parameters:
clarityStrength = 1.0
clarityRadius = 2
clarityOffset = 1.50
clarityBlendMode = 1
clarityBlendIfDark = 40
clarityBlendIfLight = 220
clarityDarkIntensity = 0.16
clarityLightIntensity = 0.16

# Adjust the ClarityCAS hybrid parameters:
clarityCasStrength = 0.5
clarityCasRadius = 1
clarityCasOffset = 2.0
clarityCasBlendMode = 0
clarityCasBlendIfDark = 20
clarityCasBlendIfLight = 240

clarityCasCasSharpness = 0.4
clarityCasCasStrength = 0.7

# Adjust the ClarityRCAS hybrid parameters:
clarityRcasCasSharpness = 0.4
clarityRcasCasStrength = 0.7

# Extended SMAA Settings (Optional):
# Uncomment to use a preset (ignores individual SMAA settings) or tweak diagonal detection:
# smaaPreset = ultra
# smaaDisableDiagDetection = 0
```

The shader (`clarity.frag.glsl`) implements a **highly optimized 8-tap axis-aligned sparse cross-convolution pattern** for local contrast enhancement using **scalar luma reconstruction, keeping register allocation low to ensure maximum performance across handhelds and low-end GPUs**. The `strength` parameter controls how much of the enhanced contrast is blended into the final image, while `radius` determines how far from each pixel the blur samples are taken.

#### Reshade Fx shaders

To run reshade fx shaders e.g. shaders from the [reshade repo](https://github.com/crosire/reshade-shaders), you have to set `reshadeTexturePath` and `reshadeIncludePath` to the matching directories from the repo. To then use a specific shader you need to set a custom effect name to the shader path and then add that effect name to `effects` like every other effect.

```ini
effects = colorfulness:denoise

colourfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders
```

#### Ingame Input

The [HOME key](https://en.wikipedia.org/wiki/Home_key) can be used to disable and re-enable the applied effects, the key can also be changed in the config file. This is based on X11 so it won't work on pure wayland. It **should** however at least not crash without X11.


#### Debug Output

The amount of debug output can be set with the `VKBASALT_LOG_LEVEL` env var, e.g. `VKBASALT_LOG_LEVEL=debug`. Possible values are: `trace, debug, info, warn, error, none`.

By default the logger outputs to stderr, a file as output location can be set with the `VKBASALT_LOG_FILE` env var, e.g. `VKBASALT_LOG_FILE="vkBasalt.log"`.


## FAQ

#### Why is it called vkBasalt?
It's a joke: vulkan post processing &#8594; after vulcan &#8594; basalt
#### Does vkBasalt work with dxvk and vkd3d?
Yes.
#### Will vkBasalt get me banned?
Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies.
#### Will there be a openGl version?
No. I don't know anything about openGl and I don't want to either. Also openGl has no layer system like vulkan.
#### Will there be a GUI in the future?
Maybe, but not soon.
#### So is vkBasalt just a reshade port for linux?
Not really, most of the code was written from scratch. vkBasalt directly uses reshade source code for the shader compiler (thanks [@crosire](https://github.com/crosire)), but that's about it.
#### Does every reshade shader work?
No. Shaders that need multiple techniques do not work, there might still be problems with stencil and blending and depth buffer access isn't ready yet.
#### You said that "depth buffer access isn't ready yet", what does this mean?
There is a wip version that you can enable with `depthCapture = on`. It will lead to many problems especially on non nvidia hardware. Also the selected depth buffer isn't always the one you would want.
#### Is there a way to change settings for reshade shaders?
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way is to simply edit the shader file.
