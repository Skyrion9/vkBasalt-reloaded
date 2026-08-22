#include "effect_crystalclear.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "effect.hpp"

namespace vkBasalt
{

int CrystalClearEffect::minQualityForParam(const std::string& key) const
{
    // Parameters disabled below certain quality levels. Returns the MAXIMUM qualityLevel at which the param is still active. If current qualityLevel > returned value, the param is disabled.
    static const std::unordered_map<std::string, int> thresholds = {
        // Perfect only (qualityLevel == 0)
        {"crystalclearEnableRGBEdgeDetection", 0},
        {"crystalclearEnableFringeFix",        0},
        {"crystalclearFringeStrength",         0},
        // Ultra+ (qualityLevel <= 1)
        {"crystalclearLocalContrastStrength",  1},
        // High+ (qualityLevel <= 2)
        {"crystalclearEnableCheckerboardFix",  2},
        {"crystalclearCheckerboardStrength",   2},
        {"crystalclearEnableDespeckle",        2},
        {"crystalclearDespeckleThreshold",     2},
        {"crystalclearEnableBC1Fix",           2},
        {"crystalclearBC1FixStrength",         2},
        // Medium+ (qualityLevel <= 3)
        {"crystalclearShimmerReduction",       3},
        {"crystalclearEnableFilmGrain",        3},
        {"crystalclearFilmGrainStrength",      3},
        {"crystalclearFilmGrainMinimum",       3},
        {"crystalclearFineGrainWeight",        3},
        {"crystalclearCoarseGrainWeight",      3},
    };
    auto it = thresholds.find(key);
    if (it != thresholds.end()) return it->second;
    return 4; // Always active at all quality levels
}

// Declarative parameter interface
const std::vector<EffectParamDesc>& CrystalClearEffect::getParamDescs() const
{
    static const std::vector<EffectParamDesc> params = {
        // Presets & Performance
        {.key = "crystalclearPreset", .label = "Preset", .type = ParamType::Combo,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.0, .step = 0.0,
         .comboOptions = {"devfav", "esports", "artifactless", "maxsharp", "antitaa", "vibrantsharp", "devfxaa", "cinematic", "film", "vivid", "noir"},
         .category = "Presets & Performance",
         .tooltip = "Curated starting points. Adjust individual sliders after selecting.\n"
                    "devfav: balanced sharpen+grain.\nesports: high clarity, low grain.\n"
                    "antitaa: maximum TAA deblur + ghost cleanup.\n"
                    "artifactless: soft, maximum protection.\nmaxsharp: aggressive sharpen.\n"
                    "vibrantsharp: sharp + color boost.\ndevfxaa: balanced + FXAA.\n"
                    "cinematic: warm tones, split toning, deband.\nfilm: strong grain, muted colors.\n"
                    "vivid: saturated, CDL color grade.\nnoir: high contrast B&W."},

        {.key = "crystalclearQualityLevel", .label = "Quality Level", .type = ParamType::Combo,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 4.0, .step = 1.0,
         .comboOptions = {"Perfect", "Ultra", "High", "Medium", "iGPU"},
         .category = "Presets & Performance",
         .tooltip = "Master switch for feature gating. Grays out disabled params.\n"
                    "Perfect: All features. RGB edge, fringe fix, wide-radius fetches, all guards. ~17 tex fetches/pixel.\n"
                    "Ultra: Drops RGB edge detection + fringe fix. Best set-and-forget for discrete GPUs.\n"
                    "High: Also drops wide step2 fetches and local contrast. ~13 fetches.\n"
                    "Medium: Also drops oiliness/silhouette gates, checkerboard, despeckle, BC1 fix, shimmer reduction, film grain, saturation/dark-smear guards.\n"
                    "iGPU: Core CAS + Clarity (step1) only. Band pass + edge mask + extreme protection + dithering. Minimum viable quality.",
         SPEC(72, qualityLevel)},

        // Sharpening & Contrast
        {.key = "crystalclearBilateralRadius", .label = "Bilateral Radius", .type = ParamType::Float,
         .defaultVal = 2.5, .minVal = 0.5, .maxVal = 8.0, .step = 0.1,
         .category = "Sharpening & Contrast",
         .tooltip = "Radius of the bilateral macro-contrast kernel. Larger values boost wider features (large-scale contrast). Smaller values focus on fine detail. Default 2.5.",
         SPEC(0, radius)},

        {.key = "crystalclearBilateralOffset", .label = "Bilateral Offset", .type = ParamType::Float,
         .defaultVal = 1.5, .minVal = 0.5, .maxVal = 3.0, .step = 0.1,
         .category = "Sharpening & Contrast",
         .tooltip = "Multiplier on the bilateral sample offset. Combined with Radius to determine actual fetch distance. Higher = wider contrast evaluation. Default 1.5.",
         SPEC(1, offset)},

        {.key = "crystalclearSharpStrength", .label = "Sharp Strength", .type = ParamType::Float,
         .defaultVal = 5.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
         .category = "Sharpening & Contrast",
         .tooltip = "Master strength of the bilateral sharpening (Clarity) pass. Controls how strongly the macro-contrast delta is applied to the image. Default 5.0.",
         SPEC(2, SharpStrength)},

        {.key = "crystalclearBlendMode", .label = "Blend Mode", .type = ParamType::Int,
         .defaultVal = 5.0, .minVal = 0.0, .maxVal = 6.0, .step = 1.0,
         .category = "Sharpening & Contrast",
         .tooltip = "How the sharpening delta blends with the original.\n"
                    "0: Soft Light\n1: Overlay\n2: Hard Light\n3: Vivid Light (clamped)\n"
                    "4: Linear Light\n5: Additive (default, best for TAA deblurring)\n6: Simple offset",
         SPEC(3, blendMode)},

        {.key = "crystalclearBlendIfDark", .label = "Blend If Dark", .type = ParamType::Int,
         .defaultVal = 8.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
         .category = "Sharpening & Contrast",
         .tooltip = "Photoshop-style 'Blend If' for shadows. Pixels darker than this value receive reduced sharpening. 0 = disabled (sharpen everything). Default 8.",
         SPEC(4, blendIfDark)},

        {.key = "crystalclearBlendIfLight", .label = "Blend If Light", .type = ParamType::Int,
         .defaultVal = 248.0, .minVal = 0.0, .maxVal = 255.0, .step = 1.0,
         .category = "Sharpening & Contrast",
         .tooltip = "Photoshop-style 'Blend If' for highlights. Pixels brighter than this value receive reduced sharpening. 255 = disabled. Default 248.",
         SPEC(5, blendIfLight)},

        {.key = "crystalclearCasSharpness", .label = "CAS Sharpness", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Sharpening & Contrast",
         .tooltip = "AMD FidelityFX CAS sharpness amount (0-1). Controls the peak weight of the CAS micro-detail filter. Higher = more fine detail, more risk of ringing. Default 1.0.",
         SPEC(6, casSharpness)},

        {.key = "crystalclearCasStrength", .label = "CAS Strength", .type = ParamType::Float,
         .defaultVal = 3.0, .minVal = 0.0, .maxVal = 5.0, .step = 0.1,
         .category = "Sharpening & Contrast",
         .tooltip = "Master multiplier on the CAS delta. Amplifies the micro-detail sharpening output. Default 3.0.",
         SPEC(7, casStrength)},

        {.key = "crystalclearLocalContrastStrength", .label = "Local Contrast", .type = ParamType::Float,
         .defaultVal = 2.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.05,
         .category = "Sharpening & Contrast",
         .tooltip = "Wide-radius local contrast boost. Compares pixel to a large-area blur to enhance macro structure. Disabled on High and below (requires step2 wide fetches). Default 2.0 (max).",
         SPEC(41, localContrastStrength)},

        // Anti-Aliasing (FXAA)
        {.key = "crystalclearEnableAA", .label = "Enable AA", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Enables integrated FXAA 3.11 anti-aliasing. Reuses CAS pixel fetches for minimal overhead. Consider SMAA or CMAA for better quality when available. Default off.",
         SPEC(11, enableAA)},

        {.key = "crystalclearFxaaEdgeThreshold", .label = "FXAA Edge Thresh", .type = ParamType::Float,
         .defaultVal = 0.05, .minVal = 0.001, .maxVal = 1.0, .step = 0.01,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Minimum local contrast to trigger FXAA edge detection. Lower = more edges detected (more blur risk). Higher = fewer edges (misses subtle aliasing). Default 0.05.",
         SPEC(13, fxaaEdgeThreshold)},

        {.key = "crystalclearFxaaEdgeThresholdMin", .label = "FXAA Edge Min", .type = ParamType::Float,
         .defaultVal = 0.0312, .minVal = 0.0, .maxVal = 1.0, .step = 0.001,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Absolute minimum edge threshold floor. Prevents FXAA from activating on noise below this level. Default 0.0312 (1/32).",
         SPEC(18, fxaaEdgeThresholdMin)},

        {.key = "crystalclearFxaaSubpixAmount", .label = "FXAA Subpix", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Sub-pixel anti-aliasing amount. Higher = smoother thin lines and diagonal edges, but can blur fine detail. 0 = disable sub-pixel AA. Default 1.0.",
         SPEC(14, fxaaSubpixAmount)},

        {.key = "crystalclearFxaaSearchScale", .label = "FXAA Search Scale", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.1, .maxVal = 3.0, .step = 0.1,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Multiplier on the FXAA edge endpoint search distance. Higher = catches longer diagonal edges, costs more on those edges. Short edges still terminate early. Default 1.0.",
         SPEC(15, fxaaSearchScale)},

        {.key = "crystalclearFxaaHardEdgeThreshold", .label = "FXAA Hard Edge", .type = ParamType::Float,
         .defaultVal = 0.08, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Threshold for 'hard' edge classification. Used internally for sub-pixel thin-line detection. Default 0.08.",
         SPEC(16, fxaaHardEdgeThreshold)},

        {.key = "crystalclearFxaaOnlyMode", .label = "FXAA Only", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Anti-Aliasing (FXAA)",
         .tooltip = "Skip all sharpening/color/grain passes, output only the FXAA result. Useful for debugging or using CrystalClear as a pure AA filter. Default off.",
         SPEC(19, fxaaOnlyMode)},

        // Artifact Protection
        {.key = "crystalclearGuardStrength", .label = "Guard Strength", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Master multiplier for all protective masks (band pass, edge mask, silhouette, saturation, dark smear, texture protection). 0 = all guards bypassed (maximum sharpening, more artifacts). 1 = full protection (conservative). Default 0.5.",
         SPEC(30, guardStrength)},

        {.key = "crystalclearBandPassWidth", .label = "Band Pass Width", .type = ParamType::Float,
         .defaultVal = 0.85, .minVal = 0.3, .maxVal = 1.5, .step = 0.05,
         .category = "Artifact Protection",
         .tooltip = "Width of the band-pass filter on local contrast. Sharpening is strongest on mid-frequency detail and fades on very low (macro structure) and very high (noise) frequencies. Larger = wider band. Default 0.85.",
         SPEC(31, bandPassWidth)},

        {.key = "crystalclearExtremeProtection", .label = "Extreme Protection", .type = ParamType::Float,
         .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Reduces sharpening at luminance extremes (very dark shadows, very bright highlights) where artifacts are most visible. 0 = no penalty, 1 = full protection. Default 0.4.",
         SPEC(32, extremeProtection)},

        {.key = "crystalclearShimmerReduction", .label = "Shimmer Reduction", .type = ParamType::Float,
         .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Stabilizes isolated bright/dark pixels that flicker between frames (shimmer). Blends them toward the local cross-average. Disabled on iGPU. Default 0.4.",
         SPEC(33, shimmerReduction)},

        {.key = "crystalclearEdgeThreshLow", .label = "Edge Thresh Low", .type = ParamType::Float,
         .defaultVal = 0.03, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Lower threshold of the bilateral edge smoothstep. Contrast differences below this are fully suppressed. Lower = more fine detail passes through. Default 0.03.",
         SPEC(8, edgeThreshLow)},

        {.key = "crystalclearEdgeThreshHigh", .label = "Edge Thresh High", .type = ParamType::Float,
         .defaultVal = 0.25, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Upper threshold of the bilateral edge smoothstep. Contrast differences above this are fully passed through. The range between Low and High is the smooth transition zone. Default 0.25.",
         SPEC(9, edgeThreshHigh)},

        {.key = "crystalclearEnableRGBEdgeDetection", .label = "RGB Edge Detection", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Per-channel edge detection (3x more ALU than luma-only). Catches chroma-only edges that luma misses (e.g., red text on dark green). Perfect quality only. Default on.",
         SPEC(12, enableRGBEdgeDetection)},

        {.key = "crystalclearClarityTextureProtection", .label = "Clarity Protection", .type = ParamType::Float,
         .defaultVal = 0.35, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Reduces CAS sharpening on micro-textures (high directional purity) to prevent amplifying noise and compression artifacts into visible ringing. Higher = more protection. Default 0.35.",
         SPEC(17, clarityTextureProtection)},

        {.key = "crystalclearEnableChromaSmooth", .label = "Chroma Smooth", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Edge-aware chroma denoiser. Blurs color channels in flat areas to kill color noise (common from TAA and compression) without softening luma detail. Default on.",
         SPEC(38, enableChromaSmooth)},

        {.key = "crystalclearChromaSmoothStrength", .label = "Chroma Strength", .type = ParamType::Float,
         .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Strength of the chroma smoothing pass. Higher = more aggressive color noise removal. Default 0.4.",
         SPEC(39, chromaSmoothStrength)},

        {.key = "crystalclearEnableDespeckle", .label = "Enable Despeckle", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Removes isolated impulse noise (random bright/dark outlier pixels) that causes the antsy micro-shimmer look. Gated by Despeckle Threshold. Disabled on Medium and below. Default on.",
         SPEC(42, enableDespeckle)},

        {.key = "crystalclearDespeckleThreshold", .label = "Despeckle Threshold", .type = ParamType::Float,
         .defaultVal = 0.15, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Artifact Protection",
         .tooltip = "Minimum isolation (difference from cross-average) to classify a pixel as a speckle. Lower = catches more noise but risks removing real detail. Default 0.15.",
         SPEC(43, despeckleThreshold)},

        {.key = "crystalclearEnableFringeFix", .label = "Fringe Fix (CA)", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Chromatic aberration fringe suppression. Detects per-channel edge disagreement and desaturates fringing zones. Requires RGB edge detection. Perfect quality only. Default off.",
         SPEC(44, enableFringeFix)},

        {.key = "crystalclearFringeStrength", .label = "Fringe Strength", .type = ParamType::Float,
         .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
         .category = "Artifact Protection",
         .tooltip = "Strength of the fringe desaturation. Higher = more aggressive CA removal. Perfect quality only. Default 0.4.",
         SPEC(45, fringeStrength)},

        {.key = "crystalclearEnableCheckerboardFix", .label = "Checkerboard Fix", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Corrects checkerboard transparency patterns used by some games for camera obstruction (e.g., foliage). Detects the alternating structure and blends toward the background estimate. Disabled on Medium and below. Default off.",
         SPEC(70, enableCheckerboardFix)},

        {.key = "crystalclearCheckerboardStrength", .label = "Checkerboard Strength", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
         .category = "Artifact Protection",
         .tooltip = "Strength of the checkerboard correction blend. Higher = more aggressive removal. Disabled on Medium and below. Default 0.5.",
         SPEC(71, checkerboardStrength)},

        {.key = "crystalclearEnableBC1Fix", .label = "BC1 Artifact Fix", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Artifact Protection",
         .tooltip = "Suppresses green/magenta color fringing from BC1/DXT1 texture compression. Detects the 6-bit green vs 5-bit R/B quantization bias in low-saturation regions and corrects it. Also boosts chroma smoothing at 4px block boundaries. Disabled on Medium and below. Default on.",
         SPEC(73, enableBC1Fix)},

        {.key = "crystalclearBC1FixStrength", .label = "BC1 Fix Strength", .type = ParamType::Float,
         .defaultVal = 0.3, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
         .category = "Artifact Protection",
         .tooltip = "Strength of BC1/DXT1 artifact correction. Higher = more aggressive green/magenta removal and block-edge chroma smoothing. Default 0.3.",
         SPEC(74, bc1FixStrength)},

        // Film Grain & Dither
        {.key = "crystalclearEnableFilmGrain", .label = "Enable Film Grain", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Film Grain & Dither",
         .tooltip = "Perceptual film grain with fine + coarse layers. Adds subtle texture that breaks up banding and gives a natural, non-digital look. Weighted by luminance (peaks at mid-gray, fades in shadows/highlights). Disabled on iGPU. Default on.",
         SPEC(23, enableFilmGrain)},

        {.key = "crystalclearFilmGrainStrength", .label = "Grain Strength", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
         .category = "Film Grain & Dither",
         .tooltip = "Master multiplier on grain amplitude. Higher = more visible grain. Disabled on iGPU. Default 1.0.",
         SPEC(24, filmGrainStrength)},

        {.key = "crystalclearFilmGrainMinimum", .label = "Grain Minimum", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 2.0, .step = 0.1,
         .category = "Film Grain & Dither",
         .tooltip = "Floor for grain intensity. Ensures a minimum grain presence even in areas where the perceptual mask evaluates low. Higher = grain everywhere. Disabled on iGPU. Default 0.0.",
         SPEC(25, filmGrainMinimum)},

        {.key = "crystalclearFineGrainWeight", .label = "Fine Grain", .type = ParamType::Float,
         .defaultVal = 0.4, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Film Grain & Dither",
         .tooltip = "Weight of the fine grain layer (1:1 pixel resolution, updates every frame). Higher = more high-frequency grain texture. Disabled on iGPU. Default 0.4.",
         SPEC(27, fineGrainWeight)},

        {.key = "crystalclearCoarseGrainWeight", .label = "Coarse Grain", .type = ParamType::Float,
         .defaultVal = 0.8, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Film Grain & Dither",
         .tooltip = "Weight of the coarse grain layer (1/4 resolution, updates every 2 frames). Higher = larger, more film-like grain clumps. Disabled on iGPU. Default 0.8.",
         SPEC(28, coarseGrainWeight)},

        {.key = "crystalclearEnableDithering", .label = "Enable Dithering", .type = ParamType::Bool,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Film Grain & Dither",
         .tooltip = "Contrast-adaptive dithering to break up color banding in gradients. Boosted in flat areas where banding is visible, reduced in textured areas. Always available at all quality levels. Default on.",
         SPEC(10, enableDithering)},

        // Color & Tone
        {.key = "crystalclearVibrance", .label = "Vibrance", .type = ParamType::Float,
         .defaultVal = 0.3, .minVal = -1.0, .maxVal = 1.0, .step = 0.05,
         .category = "Color & Tone",
         .tooltip = "Intelligent saturation that boosts muted colors more than already-saturated ones. Positive = more vibrant, negative = desaturate. 0 = off. Default 0.3.",
         SPEC(34, vibrance)},

        {.key = "crystalclearSaturation", .label = "Saturation", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.05,
         .category = "Color & Tone",
         .tooltip = "Uniform saturation adjustment applied to all colors equally. -1 = grayscale, 0 = off, +1 = double saturation. Default 0.0.",
         SPEC(46, saturation)},

        {.key = "crystalclearEnableDeband", .label = "Enable Deband", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Color & Tone",
         .tooltip = "Direction-aware debanding that breaks up color banding in flat/gradient regions by injecting noise. Gated by local contrast and edge mask to avoid affecting real detail. Default off.",
         SPEC(35, enableDeband)},

        {.key = "crystalclearDebandStrength", .label = "Deband Strength", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.05,
         .category = "Color & Tone",
         .tooltip = "Amplitude of the debanding noise. Higher = more aggressive band removal, but can add visible grain to flat areas. Default 0.5.",
         SPEC(36, debandStrength)},

        {.key = "crystalclearToneCurve", .label = "Tone Curve", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color & Tone",
         .tooltip = "Filmic highlight rolloff. Smoothly compresses luminance above the knee point (0.9 SDR, hdrNorm HDR) to prevent harsh clipping. 0 = off. Default 0.0.",
         SPEC(37, toneCurve)},

        {.key = "crystalclearSpecularDesat", .label = "Specular Desat", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color & Tone",
         .tooltip = "Desaturates very bright specular highlights toward white, mimicking real-world bloom where bright reflections lose color. 0 = off, 1 = full desaturation. Default 0.0.",
         SPEC(40, specularDesat)},

        // Color Grading
        {.key = "crystalclearEnableCDL", .label = "CDL Enable", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Color Grading",
         .tooltip = "ASC Color Decision List: per-channel Slope (gain), Offset (lift), and Power (gamma). Standard color grading tool used in film/TV. Default off.",
         SPEC(47, enableCDL)},

        {.key = "crystalclearCDLSlopeR", .label = "CDL Slope R", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Red channel gain multiplier. 1.0 = no change, >1 = boost reds. Default 1.0.",
         SPEC(48, cdlSlopeR)},

        {.key = "crystalclearCDLSlopeG", .label = "CDL Slope G", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Green channel gain multiplier. Default 1.0.",
         SPEC(49, cdlSlopeG)},

        {.key = "crystalclearCDLSlopeB", .label = "CDL Slope B", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.0, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Blue channel gain multiplier. Default 1.0.",
         SPEC(50, cdlSlopeB)},

        {.key = "crystalclearCDLOffsetR", .label = "CDL Offset R", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Red channel additive offset (lift). Positive = raise reds, negative = suppress. Default 0.0.",
         SPEC(51, cdlOffsetR)},

        {.key = "crystalclearCDLOffsetG", .label = "CDL Offset G", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Green channel additive offset. Default 0.0.",
         SPEC(52, cdlOffsetG)},

        {.key = "crystalclearCDLOffsetB", .label = "CDL Offset B", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Blue channel additive offset. Default 0.0.",
         SPEC(53, cdlOffsetB)},

        {.key = "crystalclearCDLPowerR", .label = "CDL Power R", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Red channel gamma exponent. <1 = brighten midtones, >1 = darken midtones. Default 1.0.",
         SPEC(54, cdlPowerR)},

        {.key = "crystalclearCDLPowerG", .label = "CDL Power G", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Green channel gamma exponent. Default 1.0.",
         SPEC(55, cdlPowerG)},

        {.key = "crystalclearCDLPowerB", .label = "CDL Power B", .type = ParamType::Float,
         .defaultVal = 1.0, .minVal = 0.1, .maxVal = 4.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Blue channel gamma exponent. Default 1.0.",
         SPEC(56, cdlPowerB)},

        {.key = "crystalclearEnableSplitTone", .label = "Split Tone Enable", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Color Grading",
         .tooltip = "Cinematic split toning: tints shadows and highlights independently. Luminance-preserving (shifts hue without changing brightness). Default off.",
         SPEC(57, enableSplitTone)},

        {.key = "crystalclearSTShadowR", .label = "ST Shadow R", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Shadow tint red channel. Default 0.0.",
         SPEC(58, stShadowR)},

        {.key = "crystalclearSTShadowG", .label = "ST Shadow G", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Shadow tint green channel. Default 0.5.",
         SPEC(59, stShadowG)},

        {.key = "crystalclearSTShadowB", .label = "ST Shadow B", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Shadow tint blue channel. Default 0.5.",
         SPEC(60, stShadowB)},

        {.key = "crystalclearSTHighR", .label = "ST Highlight R", .type = ParamType::Float,
         .defaultVal = 0.5, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Highlight tint red channel. Default 0.5.",
         SPEC(61, stHighR)},

        {.key = "crystalclearSTHighG", .label = "ST Highlight G", .type = ParamType::Float,
         .defaultVal = 0.3, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Highlight tint green channel. Default 0.3.",
         SPEC(62, stHighG)},

        {.key = "crystalclearSTHighB", .label = "ST Highlight B", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Highlight tint blue channel. Default 0.0.",
         SPEC(63, stHighB)},

        {.key = "crystalclearSplitToneStrength", .label = "Split Tone Strength", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Master intensity of the split toning effect. 0 = no tint, 1 = full strength. Default 0.0.",
         SPEC(64, splitToneStrength)},

        {.key = "crystalclearTemperature", .label = "Temperature", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "White balance temperature. Positive = warm (amber), negative = cool (blue). Luminance-preserving. Default 0.0 (neutral).",
         SPEC(65, temperature)},

        {.key = "crystalclearTint", .label = "Tint", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -1.0, .maxVal = 1.0, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "White balance tint along the green-magenta axis. Positive = green, negative = magenta. Luminance-preserving. Default 0.0 (neutral).",
         SPEC(66, tint)},

        {.key = "crystalclearGammaAdjust", .label = "Gamma", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = -0.9, .maxVal = 0.9, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Mid-tone brightness adjustment via gamma curve. Positive = brighter midtones, negative = darker. Does not affect pure black or white. Default 0.0 (off).",
         SPEC(67, gammaAdjust)},

        {.key = "crystalclearBlackLift", .label = "Black Point Lift", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.5, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Raises the black floor for a faded/film look. Linearly lifts dark values toward gray. 0 = off, 0.5 = maximum lift (very washed out). Default 0.0.",
         SPEC(68, blackLift)},

        {.key = "crystalclearWhiteClip", .label = "White Point Clip", .type = ParamType::Float,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 0.5, .step = 0.01,
         .category = "Color Grading",
         .tooltip = "Lowers the white ceiling. Clips bright values downward. 0 = off, 0.5 = maximum clip (crushed highlights). Default 0.0.",
         SPEC(69, whiteClip)},

        // Debug
        {.key = "crystalclearEnableDebugAA", .label = "Debug AA", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Debug",
         .tooltip = "Visualizes FXAA activity as a red heat overlay. Brighter red = more anti-aliasing applied to that pixel. Useful for tuning FXAA thresholds. Default off.",
         SPEC(20, enableDebugAA)},

        {.key = "crystalclearEnableDebugCAS", .label = "Debug CAS", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Debug",
         .tooltip = "Visualizes CAS micro-detail sharpening as a green heat overlay. Brighter green = more CAS delta applied. Useful for verifying CAS mask effectiveness. Default off.",
         SPEC(21, enableDebugCAS)},

        {.key = "crystalclearEnableDebugClarity", .label = "Debug Clarity", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Debug",
         .tooltip = "Visualizes bilateral Clarity sharpening as a cyan heat overlay. Brighter cyan = more macro-contrast delta applied. Useful for tuning bilateral radius/thresholds. Default off.",
         SPEC(22, enableDebugClarity)},

        {.key = "crystalclearEnableDebugGrain", .label = "Debug Grain", .type = ParamType::Bool,
         .defaultVal = 0.0, .minVal = 0.0, .maxVal = 1.0, .step = 1.0,
         .category = "Debug",
         .tooltip = "Visualizes film grain intensity as a blue-to-yellow heat overlay. Blue = low grain, yellow = high grain. Useful for tuning grain weights and perceptual mask. Default off.",
         SPEC(26, enableDebugGrain)},
    };
    return params;
}

} // namespace vkBasalt
