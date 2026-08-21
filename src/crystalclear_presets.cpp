#include "effect_crystalclear.hpp"
#include <string>
#include <unordered_map>

namespace vkBasalt
{

const std::unordered_map<std::string, PresetMap>& getPresetTable()
{
    static const std::unordered_map<std::string, PresetMap> presetTable = {

        {"esports", {
            // Sharpening
            {"crystalclearSharpStrength",         2.0},
            {"crystalclearCasStrength",           2.5},
            {"crystalclearLocalContrastStrength", 2.0},
            // Protection
            {"crystalclearQualityLevel",          1},
            {"crystalclearGuardStrength",         0.6},
            {"crystalclearExtremeProtection",     0.5},
            {"crystalclearShimmerReduction",      0.6},
            {"crystalclearEnableDespeckle",       1},
            {"crystalclearDespeckleThreshold",    0.18},
            {"crystalclearEnableChromaSmooth",    1},
            {"crystalclearChromaSmoothStrength",  0.3},
            // Color & Tone
            {"crystalclearVibrance",              0.0},
            {"crystalclearSaturation",            0.1},
            {"crystalclearToneCurve",             0.2},
            // Grain (off for competitive clarity)
            {"crystalclearEnableFilmGrain",       0},
            {"crystalclearFilmGrainStrength",     0.8},
            // Blend range
            {"crystalclearBlendIfDark",           15},
            {"crystalclearBlendIfLight",          240},
        }},

        {"antitaa", {
            // Sharpening (maximum to counter TAA blur)
            {"crystalclearSharpStrength",            3.0},
            {"crystalclearCasStrength",              3.5},
            {"crystalclearCasSharpness",             1.0},
            {"crystalclearLocalContrastStrength",    1.0},
            // Protection (targeted against TAA artifacts)
            {"crystalclearQualityLevel",             0},
            {"crystalclearGuardStrength",            0.6},
            {"crystalclearExtremeProtection",        0.3},
            {"crystalclearShimmerReduction",         0.8},
            {"crystalclearBandPassWidth",            0.9},
            {"crystalclearEdgeThreshLow",            0.02},
            {"crystalclearEdgeThreshHigh",           0.20},
            {"crystalclearClarityTextureProtection", 0.2},
            {"crystalclearEnableRGBEdgeDetection",   1},
            // TAA ghost cleanup suite
            {"crystalclearEnableDespeckle",          1},
            {"crystalclearDespeckleThreshold",       0.12},
            {"crystalclearEnableChromaSmooth",       1},
            {"crystalclearChromaSmoothStrength",     0.5},
            // TAA compression banding
            {"crystalclearEnableDeband",             1},
            {"crystalclearDebandStrength",           0.4},
            {"crystalclearEnableDithering",          1},
            // Color (TAA slightly desaturates)
            {"crystalclearVibrance",                 0.1},
            // Grain off (pure sharpness, grain interferes with deblur)
            {"crystalclearEnableFilmGrain",          0},
        }},

        {"artifactless", {
            // Sharpening (soft)
            {"crystalclearSharpStrength",            1.2},
            {"crystalclearCasStrength",              1.5},
            {"crystalclearLocalContrastStrength",    0.0},
            // Protection (maximum)
            {"crystalclearQualityLevel",             0},
            {"crystalclearGuardStrength",            0.9},
            {"crystalclearExtremeProtection",        0.8},
            {"crystalclearShimmerReduction",         0.8},
            {"crystalclearBandPassWidth",            0.6},
            {"crystalclearEdgeThreshLow",            0.05},
            {"crystalclearEdgeThreshHigh",           0.35},
            {"crystalclearClarityTextureProtection", 0.6},
            // Artifact cleanup suite
            {"crystalclearEnableDespeckle",          1},
            {"crystalclearDespeckleThreshold",       0.15},
            {"crystalclearEnableFringeFix",          1},
            {"crystalclearFringeStrength",           0.4},
            {"crystalclearEnableCheckerboardFix",    1},
            {"crystalclearCheckerboardStrength",     0.4},
            {"crystalclearEnableChromaSmooth",       1},
            {"crystalclearChromaSmoothStrength",     0.4},
            {"crystalclearEnableDeband",             1},
            {"crystalclearDebandStrength",           0.4},
            // Color & Grain
            {"crystalclearSpecularDesat",            0.2},
            {"crystalclearEnableFilmGrain",          0},
        }},

        {"maxsharp", {
            // Sharpening (aggressive)
            {"crystalclearSharpStrength",            3.5},
            {"crystalclearCasStrength",              4.0},
            {"crystalclearCasSharpness",             1.0},
            {"crystalclearLocalContrastStrength",    1.5},
            // Protection (minimal)
            {"crystalclearQualityLevel",             0},
            {"crystalclearGuardStrength",            0.2},
            {"crystalclearExtremeProtection",        0.1},
            {"crystalclearShimmerReduction",         0.2},
            {"crystalclearBandPassWidth",            1.2},
            {"crystalclearEdgeThreshLow",            0.02},
            {"crystalclearEdgeThreshHigh",           0.20},
            {"crystalclearClarityTextureProtection", 0.1},
            {"crystalclearEnableRGBEdgeDetection",   1},
            // Pre-sharpen cleanup
            {"crystalclearEnableDespeckle",          1},
            {"crystalclearDespeckleThreshold",       0.1},
            {"crystalclearEnableChromaSmooth",       1},
            {"crystalclearChromaSmoothStrength",     0.3},
            // Grain
            {"crystalclearFilmGrainStrength",        0.8},
        }},

        {"vibrantsharp", {
            // Sharpening
            {"crystalclearSharpStrength",        2.5},
            {"crystalclearCasStrength",          2.5},
            // Protection
            {"crystalclearQualityLevel",         1},
            {"crystalclearGuardStrength",        0.5},
            {"crystalclearExtremeProtection",    0.4},
            {"crystalclearShimmerReduction",     0.5},
            {"crystalclearEnableDespeckle",      1},
            {"crystalclearDespeckleThreshold",   0.15},
            {"crystalclearEnableChromaSmooth",   1},
            {"crystalclearChromaSmoothStrength", 0.4},
            {"crystalclearEnableDeband",         1},
            {"crystalclearDebandStrength",       0.6},
            // Color & Tone
            {"crystalclearVibrance",             0.6},
            {"crystalclearSpecularDesat",        0.2},
            {"crystalclearTemperature",          0.05},
            {"crystalclearToneCurve",            0.2},
            // Grain
            {"crystalclearFilmGrainStrength",    0.6},
        }},

        {"devfxaa", {
            // Sharpening
            {"crystalclearSharpStrength",         2.0},
            {"crystalclearCasStrength",           2.5},
            // FXAA
            {"crystalclearQualityLevel",          1},
            {"crystalclearEnableAA",              1},
            {"crystalclearFxaaEdgeThreshold",     0.04},
            {"crystalclearFxaaSubpixAmount",      0.8},
            {"crystalclearFxaaSearchScale",       1.5},
            {"crystalclearFxaaHardEdgeThreshold", 0.06},
            // Protection
            {"crystalclearGuardStrength",         0.6},
            {"crystalclearShimmerReduction",      0.5},
            {"crystalclearEnableDespeckle",       1},
            {"crystalclearDespeckleThreshold",    0.15},
            {"crystalclearEnableChromaSmooth",    1},
            {"crystalclearChromaSmoothStrength",  0.3},
            // Grain
            {"crystalclearFilmGrainStrength",     0.8},
        }},

        {"cinematic", {
            // Sharpening (soft)
            {"crystalclearSharpStrength",        1.8},
            {"crystalclearCasStrength",          1.5},
            // Protection
            {"crystalclearQualityLevel",         0},
            {"crystalclearGuardStrength",        0.7},
            {"crystalclearExtremeProtection",    0.6},
            {"crystalclearShimmerReduction",     0.5},
            {"crystalclearBandPassWidth",        0.7},
            // Cleanup
            {"crystalclearEnableDespeckle",      1},
            {"crystalclearDespeckleThreshold",   0.15},
            {"crystalclearEnableChromaSmooth",   1},
            {"crystalclearChromaSmoothStrength", 0.6},
            // Film grain (warm, coarse)
            {"crystalclearFilmGrainStrength",    1.2},
            {"crystalclearFilmGrainMinimum",     0.1},
            {"crystalclearFineGrainWeight",      0.5},
            {"crystalclearCoarseGrainWeight",    0.9},
            // Lens simulation
            {"crystalclearEnableFringeFix",      1},
            {"crystalclearFringeStrength",       0.3},
            {"crystalclearSpecularDesat",        0.4},
            // Tone shaping
            {"crystalclearToneCurve",            0.5},
            {"crystalclearGammaAdjust",         -0.05},
            {"crystalclearBlackLift",            0.08},
            {"crystalclearWhiteClip",            0.05},
            // Color grading (warm shadows, amber highlights)
            {"crystalclearVibrance",            -0.1},
            {"crystalclearTemperature",          0.1},
            {"crystalclearEnableSplitTone",      1},
            {"crystalclearSplitToneStrength",    0.3},
            {"crystalclearSTShadowR",            0.0},
            {"crystalclearSTShadowG",            0.4},
            {"crystalclearSTShadowB",            0.5},
            {"crystalclearSTHighR",              0.5},
            {"crystalclearSTHighG",              0.3},
            {"crystalclearSTHighB",              0.0},
            // Deband & blend range
            {"crystalclearEnableDeband",         1},
            {"crystalclearDebandStrength",       0.5},
            {"crystalclearBlendIfDark",          20},
            {"crystalclearBlendIfLight",         230},
        }},

        {"film", {
            // Sharpening (soft)
            {"crystalclearSharpStrength",        1.5},
            {"crystalclearCasStrength",          1.5},
            // Protection
            {"crystalclearQualityLevel",         0},
            {"crystalclearGuardStrength",        0.7},
            {"crystalclearExtremeProtection",    0.7},
            {"crystalclearShimmerReduction",     0.6},
            // Cleanup
            {"crystalclearEnableDespeckle",      1},
            {"crystalclearDespeckleThreshold",   0.15},
            {"crystalclearEnableChromaSmooth",   1},
            {"crystalclearChromaSmoothStrength", 0.5},
            // Film grain (heavy, textured)
            {"crystalclearFilmGrainStrength",    1.5},
            {"crystalclearFilmGrainMinimum",     0.2},
            {"crystalclearFineGrainWeight",      0.6},
            {"crystalclearCoarseGrainWeight",    0.9},
            // Lens simulation
            {"crystalclearEnableFringeFix",      1},
            {"crystalclearFringeStrength",       0.25},
            {"crystalclearSpecularDesat",        0.3},
            // Tone shaping (faded, muted)
            {"crystalclearToneCurve",            0.4},
            {"crystalclearGammaAdjust",         -0.1},
            {"crystalclearBlackLift",            0.15},
            {"crystalclearWhiteClip",            0.1},
            // Color grading (desaturated, cool shadows)
            {"crystalclearSaturation",          -0.2},
            {"crystalclearTemperature",          0.05},
            {"crystalclearEnableSplitTone",      1},
            {"crystalclearSplitToneStrength",    0.25},
            {"crystalclearSTShadowR",            0.0},
            {"crystalclearSTShadowG",            0.3},
            {"crystalclearSTShadowB",            0.4},
            {"crystalclearSTHighR",              0.4},
            {"crystalclearSTHighG",              0.25},
            {"crystalclearSTHighB",              0.0},
            // Deband
            {"crystalclearEnableDeband",         1},
            {"crystalclearDebandStrength",       0.5},
        }},

        {"vivid", {
            // Sharpening
            {"crystalclearSharpStrength",        2.5},
            {"crystalclearCasStrength",          3.0},
            // Protection
            {"crystalclearQualityLevel",         1},
            {"crystalclearGuardStrength",        0.4},
            {"crystalclearShimmerReduction",     0.5},
            {"crystalclearEnableDespeckle",      1},
            {"crystalclearDespeckleThreshold",   0.15},
            {"crystalclearEnableChromaSmooth",   1},
            {"crystalclearChromaSmoothStrength", 0.3},
            {"crystalclearEnableDeband",         1},
            {"crystalclearDebandStrength",       0.4},
            // Color pop
            {"crystalclearVibrance",             0.3},
            {"crystalclearSaturation",           0.4},
            {"crystalclearTemperature",          0.05},
            {"crystalclearSpecularDesat",        0.2},
            {"crystalclearToneCurve",            0.15},
            // CDL grade (warm reds, lifted greens)
            {"crystalclearEnableCDL",            1},
            {"crystalclearCDLSlopeR",            1.1},
            {"crystalclearCDLSlopeG",            1.05},
            {"crystalclearCDLSlopeB",            1.0},
            // Grain
            {"crystalclearFilmGrainStrength",    0.6},
        }},

        {"noir", {
            // Sharpening (high contrast)
            {"crystalclearSharpStrength",        3.0},
            {"crystalclearCasStrength",          3.5},
            // Protection (minimal)
            {"crystalclearQualityLevel",         1},
            {"crystalclearGuardStrength",        0.3},
            {"crystalclearExtremeProtection",    0.2},
            {"crystalclearShimmerReduction",     0.3},
            // Cleanup
            {"crystalclearEnableDespeckle",      1},
            {"crystalclearDespeckleThreshold",   0.15},
            {"crystalclearEnableChromaSmooth",   1},
            {"crystalclearChromaSmoothStrength", 0.3},
            // B&W conversion
            {"crystalclearSaturation",          -1.0},
            // Tone shaping (crushed, contrasty)
            {"crystalclearToneCurve",            0.3},
            {"crystalclearGammaAdjust",          0.2},
            {"crystalclearBlackLift",            0.05},
            {"crystalclearWhiteClip",            0.15},
            // Grain (visible, filmic)
            {"crystalclearFilmGrainStrength",    1.0},
            {"crystalclearFilmGrainMinimum",     0.3},
            {"crystalclearFineGrainWeight",      0.5},
            {"crystalclearCoarseGrainWeight",    0.7},
            // Banding protection
            {"crystalclearEnableDeband",         1},
            {"crystalclearDebandStrength",       0.5},
            {"crystalclearEnableDithering",      1},
        }},

    };
    return presetTable;
}

} // namespace vkBasalt
