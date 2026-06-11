```markdown
# Custom VKBasalt GLSL Clarity Implementation
> * **Integrated Custom Clarity Effect:** Added a native, single-pass GLSL implementation of the local contrast enhancement filter directly into the built-in effects stack.
> * **Axis-Aligned Cross-Convolution:** Replaced high-overhead multi-pass methods from legacy `Clarity.fx` with a branchless, 8-tap horizontal/vertical cross-convolution layout to match axis-aligned game geometry perfectly while maintaining matching image quality.
> * **Zero-Intermediate Texture Allocation:** Completely eliminated the half-resolution auxiliary render targets (`ClarityTex` and `ClarityTex2`) utilized by traditional `Clarity.fx` for downsample and blur passes. Running strictly in a textureless single pass avoids intermediate frame-buffer blits and drastically cuts VRAM bandwidth consumption and overheads.Negating Clarity's perhaps the only downside.
> * **Hardware Bilinear Exploitation:** Utilizes fractional sample strides (`1.5` and `4.5` texels out) to offload interpolation to the GPU's native hardware bilinear filtering units, securing wide-radius blurring footprints with zero extra ALU performance cost.
> * **Scalar Luma Reconstruction:** Abandoned the traditional ReShade method of splitting color vectors into explicit chrominance arrays (`color / luma`). By executing operations entirely through a late-stage single float multiplier (`lumaScale`), the implementation alleviates vector register pressure and maximizes GPU wavefront occupancy.
> * **Branchless Pipeline Flattening:** Swapped the conditional if/else runtime forks found in `Clarity.fx` for a branchless ternary selection pattern. This compiles directly into hardware execution predicates (`CSEL`), completely preventing thread stalls inside the GPU execution warp.
> * **Exposed Full 8 Parameter Configuration in VKBasalt.conf:** Staying faithful to the original shader this is fully configurable with all the knobs.

---

## Building with scripts (Optimized flags)
1. Simply clone, or downlaod as zip and extract this repo.  
2. Run either script below. Rminder it prompts for su password as it's a system-wide lib 

Both come with march native optimized compiler flags and Thin LTO.

Open project folder in terminal and run:

### Fish
```
chmod +x ./build_vkbasalt_native_optimized.fish
./build_vkbasalt_native_optimized.fish
```

Or

### Bash
```
chmod +x ./build_vkbasalt_native_optimized.sh
./build_vkbasalt_native_optimized.sh
```

# vkBasalt
vkBasalt is a Vulkan post processing layer to enhance the visual graphics of games.

Currently, the build in effects are:
- Contrast Adaptive Sharpening
- Denoised Luma Sharpening
- Fast Approximate Anti-Aliasing
- Enhanced Subpixel Morphological Anti-Aliasing
- 3D color LookUp Table
**- Clarity (Optimized Single-Pass Local Contrast Enhancement)**

It is also possible to use Reshade Fx shaders.

## Disclaimer
This is one of my first projects ever, so expect it to have bugs. Use it at your own risk.

## Building from Source

### Dependencies
Before building, you will need:
- GCC >= 9
- X11 development files
- glslang
- SPIR-V Headers
- Vulkan Headers

### Building

**These instructions use `--prefix=/usr`, which is generally not recommened since vkBasalt will be installed in directories that are meant for the package manager. The alternative is not setting the prefix, it will then be installed in `/usr/local`. But you need to make sure that `ld` finds the library since /usr/local is very likely not in the default path.** 

In general, prefer using distro provided packages.

```
git clone https://github.com/DadSchoorse/vkBasalt.git
cd vkBasalt
```

#### 64bit

```
meson setup --buildtype=release --prefix=/usr builddir
ninja -C builddir install
```
#### 32bit

Make sure that `PKG_CONFIG_PATH=/usr/lib32/pkgconfig` and `--libdir=lib32` are correct for your distro and change them if needed. On Debian based distros you need to replace `lib32` with `lib/i386-linux-gnu`, for example.
```
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
* a file set with the environment variable`VKBASALT_CONFIG_FILE=/path/to/vkBasalt.conf`
* `vkBasalt.conf` in the working directory of the game
* `$XDG_CONFIG_HOME/vkBasalt/vkBasalt.conf` or `~/.config/vkBasalt/vkBasalt.conf` if `XDG_CONFIG_HOME` is not set
* `$XDG_DATA_HOME/vkBasalt/vkBasalt.conf` or `~/.local/share/vkBasalt/vkBasalt.conf` if `XDG_DATA_HOME` is not set
* `/etc/vkBasalt.conf`
* `/etc/vkBasalt/vkBasalt.conf`
* `/usr/share/vkBasalt/vkBasalt.conf`

If you want to make changes for one game only, you can create a file named `vkBasalt.conf` in the working directory of the game and change the values there.

#### Clarity Configuration
Here's how to configure clarity's VKBasalt implementation in vkBasalt.conf:

**The newly expanded clarity effect has eight fully configurable parameters parsed natively by the pipeline:**

### 1. `clarityStrength` (default: **0.60**)

* **Purpose**: Controls the amount of contrast enhancement
* **Range**: 0.0 (no effect) to 1.0 (maximum enhancement)
* **Default**: **1.00 (Recommend 0.6 if it's too much for you :) )**

### 2. `clarityRadius` (default: **2**)

* **Purpose**: Controls the base component radius of the local contrast effect in pixels
* **Range**: Recommended 1 - **5**

**### 3. clarityOffset (default: 1.50)**

* **Purpose: Fine-tunes the physical sampling step size of the sparse cross-convolution footprint. Paired with native GPU bilinear texture filtering, fractional offsets (e.g., 1.5 and 4.5) stretch the sampling area efficiently across wider pixel zones without introducing cash misses or extra texture fetch instructions.**

**### 4. clarityBlendMode (default: 1)**

* **Purpose: Changes the mathematical equation used to composite the contrast mask.**
* **Supported Operational Constants:**
* **0: Soft Light (Highly subtle local variance)**
* **1: Overlay (Default: Ideal balance using a smooth S-curve blend model to preserve native tone distributions)**
* **2: Hard Light**
* **3: Multiply**
* **4: Vivid Light**
* **5: Linear Light**
* **6: Addition (Raw value stack; can lead to clipping if used with extreme radius configurations)**



**### 5. clarityBlendIfDark (default: 40) & clarityBlendIfLight (default: 220)**

* **Purpose: Hardware mid-tone luminance masking gates. Coordinates with an unweighted average function to isolate the effect from deep crushing blacks (values below BlendIfDark) or clipping stark highlights/skyboxes (values above BlendIfLight). Values scale on a traditional 0-255 depth spectrum.**

**### 6. clarityDarkIntensity (default: 0.16) & clarityLightIntensity (default: 0.16)**

* **Purpose: Symmetrical halo attenuation parameters.**
* **Crucial Pipeline Details: These parameters act as subtractive inversion weights inside the math engine ($1.0 - \text{Intensity}$). Pushing these coefficients closer to 1.0 filters out thick black and white rings near extreme contrast thresholds, localizing the clarity effect directly to native game texture structures. However it will make the contrast less punchy. The default is a delicate balance. Recommended ranges 0.1 - 0.3**

## How to Enable and Configure

In your `~/.config/vkBasalt/vkBasalt.conf` file (or wherever the final config is installed), modify:

```ini
# Enable clarity effect - replace 'cas' with 'clarity'
effects = clarity

# Or combine multiple effects (they run left to right):
# effects = clarity:cas

# Adjust the clarity parameters:
**clarityStrength = 1.0**
**clarityRadius = 2**
**clarityOffset = 1.50**
**clarityBlendMode = 1**
**clarityBlendIfDark = 40**
**clarityBlendIfLight = 220**
**clarityDarkIntensity = 0.16**
**clarityLightIntensity = 0.16**

```

The shader (`clarity.frag.glsl`) implements a **highly optimized 8-tap axis-aligned sparse cross-convolution pattern** for local contrast enhancement using **scalar luma reconstruction, keeping register allocation low to ensure maximum performance across handhelds and low-end GPUs**. The `strength` parameter controls how much of the enhanced contrast is blended into the final image, while `radius` determines how far from each pixel the blur samples are taken.

#### Reshade Fx shaders

To run reshade fx shaders e.g. shaders from the [reshade repo](https://github.com/crosire/reshade-shaders), you have to set `reshadeTexturePath` and `reshadeIncludePath` to the matching dirctories from the repo. To then use a specific shader you need to set a custom effect name to the shader path and then add that effect name to `effects` like every other effect.

```ini
effects = colorfulness:denoise

colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
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
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way so to simply edit the shader file.
