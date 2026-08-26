#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

// ===============================================================================
// crystalclear: All-in-one spatial filter combining bilateral macro-contrast,
// CAS micro-sharpening, FXAA anti-aliasing, film grain, and artifact guards.
// Single pass, reuses fetches across stages for peak performance.
// Intelligently masks effects & amplifies best of both worlds instead of artifacts.
// Combat TAA blur & artifacts, enhanced image quality with heuristics.
// ===============================================================================
//
// Portions derived from AMD FidelityFX CAS:
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Portions derived from FXAA 3.11:
// Copyright (c) 2010, 2011 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2010, 2011 Timothy Lottes. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the NVIDIA Corporation nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Bilateral sharpening inspired by ReShade's Clarity shader.
// ===============================================================================
// The finetuned sharpen filters effectively counteract TAA blur without the eye-straining artifacts. We mask or filter out the worst artifacts.
// Single pass, absolute peak performance we re-use the same fetches and calculations whenever possible, saving about 50% frametime delay as opposed to running these as seperate shaders back-to-back.
// They are also aware of each other and are adjusted to complement each other.
// A clarity-inspired bilateral sharpen, CAS, FXAA, dithering and filmgrain to add subtle, non-distracting "detail"
// We effectively increase macro-contrast (bilateral sharpen), micro detail (CAS), reduce aliasing/shimmering (FXAA + extras) and a natural texture (subtle film grain)
// Note that FXAA is disabled by default and you should use SMAA/CMAA where possible. It's a great alternative for extra FPS as it re-uses CAS' pixel fetches.
//
// HDR:'hdrNorm' is the local adaptation luminance (SDR=1.0). Used for values above SDR white instead of collapsing to zero.
// Guard: 'guardStrength' is a master multiplier (0-1) for all protective masks.
// At 0, guards are bypassed, more artifacts. At 1, no artifacting but minor effect (Only sharpen if 100% certain no artifacting.)
// Individual knobs (bandPassWidth, extremeProtection, shimmerReduction) give targeted control.

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float radius = 3.5;
layout(constant_id = 1) const float offset = 1.5;
layout(constant_id = 2) const float clarityStrength = 1.0;
layout(constant_id = 3) const int blendMode = 1;
layout(constant_id = 4) const int blendIfDark = 40;
layout(constant_id = 5) const int blendIfLight = 220;
layout(constant_id = 6) const float casSharpness = 1.0;
layout(constant_id = 7) const float casStrength = 2.0;
layout(constant_id = 8) const float edgeThreshLow = 0.05;
layout(constant_id = 9) const float edgeThreshHigh = 0.35;
layout(constant_id = 10) const int enableDithering = 1;
layout(constant_id = 11) const int enableAA = 0;
layout(constant_id = 12) const int enableRGBEdgeDetection = 1;
layout(constant_id = 13) const float fxaaEdgeThreshold = 0.0625;
layout(constant_id = 14) const float fxaaSubpixAmount = 1.0;
layout(constant_id = 15) const float fxaaSearchScale = 1.0;
layout(constant_id = 16) const float fxaaHardEdgeThreshold = 0.08;
layout(constant_id = 17) const float clarityTextureProtection = 0.5;
layout(constant_id = 18) const float fxaaEdgeThresholdMin = 0.0312;
layout(constant_id = 19) const int fxaaOnlyMode = 0;
layout(constant_id = 20) const int enableDebugAA = 0;
layout(constant_id = 21) const int enableDebugCAS = 0;
layout(constant_id = 22) const int enableDebugClarity = 0;
layout(constant_id = 23) const int enableFilmGrain = 1;
layout(constant_id = 24) const float filmGrainStrength = 1.5;
layout(constant_id = 25) const float filmGrainMinimum = 0.4;
layout(constant_id = 26) const int enableDebugGrain = 0;
layout(constant_id = 27) const float fineGrainWeight = 0.6;
layout(constant_id = 28) const float coarseGrainWeight = 0.4;
layout(constant_id = 30) const float guardStrength = 0.6;
layout(constant_id = 31) const float bandPassWidth = 0.8;
layout(constant_id = 32) const float extremeProtection = 0.5;
layout(constant_id = 33) const float shimmerReduction = 0.5;
layout(constant_id = 34) const float vibrance = 0.0;              // -1.0 to 1.0
layout(constant_id = 35) const int enableDeband = 0;
layout(constant_id = 36) const float debandStrength = 0.5;
layout(constant_id = 37) const float toneCurve = 0.0;             // filmic highlight rolloff, 0.0 = off
layout(constant_id = 38) const int enableChromaSmooth = 0;
layout(constant_id = 39) const float chromaSmoothStrength = 0.5;
layout(constant_id = 40) const float specularDesat = 0.0;         // 0.0 = off, 0.4 = subtle, 1.0 = max
layout(constant_id = 41) const float localContrastStrength = 0.0; // Clarity local contrast knob
layout(constant_id = 42) const int enableDespeckle = 0;
layout(constant_id = 43) const float despeckleThreshold = 0.15;
layout(constant_id = 44) const int enableFringeFix = 0;
layout(constant_id = 45) const float fringeStrength = 0.5;
layout(constant_id = 46) const float saturation = 0.0;            // -1.0 (grayscale) to 1.0 (double), 0.0 = off
layout(constant_id = 47) const int enableCDL = 0;
layout(constant_id = 48) const float cdlSlopeR = 1.0;
layout(constant_id = 49) const float cdlSlopeG = 1.0;
layout(constant_id = 50) const float cdlSlopeB = 1.0;
layout(constant_id = 51) const float cdlOffsetR = 0.0;
layout(constant_id = 52) const float cdlOffsetG = 0.0;
layout(constant_id = 53) const float cdlOffsetB = 0.0;
layout(constant_id = 54) const float cdlPowerR = 1.0;
layout(constant_id = 55) const float cdlPowerG = 1.0;
layout(constant_id = 56) const float cdlPowerB = 1.0;
layout(constant_id = 57) const int enableSplitTone = 0;
layout(constant_id = 58) const float stShadowR = 0.0;
layout(constant_id = 59) const float stShadowG = 0.5;
layout(constant_id = 60) const float stShadowB = 0.5;
layout(constant_id = 61) const float stHighR = 0.5;
layout(constant_id = 62) const float stHighG = 0.3;
layout(constant_id = 63) const float stHighB = 0.0;
layout(constant_id = 64) const float splitToneStrength = 0.0;
layout(constant_id = 65) const float temperature = 0.0;            // -1.0 (cool/blue) to 1.0 (warm/amber), 0 = neutral
layout(constant_id = 66) const float tint = 0.0;                   // -1.0 (magenta) to 1.0 (green), 0 = neutral
layout(constant_id = 67) const float gammaAdjust = 0.0;            // -0.9 to 0.9, 0 = off
layout(constant_id = 68) const float blackLift = 0.0;              // 0.0 to 0.5, raises black floor (faded film)
layout(constant_id = 69) const float whiteClip = 0.0;              // 0.0 to 0.5, lowers white ceiling
layout(constant_id = 70) const int enableCheckerboardFix = 0;      // removes checker board transparency effect used for camera obstruction
layout(constant_id = 71) const float checkerboardStrength = 0.5;
layout(constant_id = 72) const int qualityLevel = 0;               // 0=Perfect, 1=Ultra, 2=High, 3=Medium, 4=iGPU
layout(constant_id = 73) const int enableBC1Fix = 0;               // BC1/DXT1 green/magenta artifact suppression
layout(constant_id = 74) const float bc1FixStrength = 0.3;         // 0.0 = off, 1.0 = maximum correction
layout(constant_id = 75) const float exposure = 0.0;               // -2.0 to 2.0 stops
layout(constant_id = 76) const float brightness = 0.0;             // -0.5 to 0.5 additive lift
layout(constant_id = 77) const float contrast = 0.0;               // -1.0 to 1.0 linear scale
layout(constant_id = 78) const float sCurveStrength = 0.0;         // 0.0 to 1.0 hermite midtone push

// push constants for spatial geometry data
layout(push_constant) uniform PushConstants {
    vec2 step1;
    vec2 step2;
    vec2 pixelSize;
} pc;

// uniform buffer object (UBO) for per-frame temporal data
layout(set = 0, binding = 1) uniform FrameData {
    uint frameCounter;
} frameData;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

const bool isHDR = (colorSpaceMode != CSP_SDR_SRGB);

// perceptual green heavy luma for edge detection, sharpening masks, and FXAA.
float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

float getNeutralLuma(vec3 rgb) {
    return (rgb.r + rgb.g + rgb.b) * 0.33333333;
}

// invThreshRange = 1.0 / (threshHigh - threshLow), precomputed once per pixel to avoid 16 divisions inside smoothstep. Manual smoothstep expansion.
float bilateralDiff(float d, float weight, float threshLow, float invThreshRange) {
    float t = clamp((abs(d) - threshLow) * invThreshRange, 0.0, 1.0);
    return d * (1.0 - t * t * (3.0 - 2.0 * t)) * weight;
}

// HDR: 'norm' is the adaptation factor (hdrNorm). Mode 5 (Linear Light) is additive, self-normalizing via the downstream luma ratio, so it stays on raw values.
// The other modes are evaluated on normalized NEUTRAL luma in HDR so their 1.0/clamp/step reference points remain valid above SDR white.
float applyBlendMode(float luma, float sharp, float norm) {
    if (blendMode == 5) return luma + (sharp - 0.5) * norm;

    float nl = (isHDR) ? luma / norm : luma;
    float r;
    if (blendMode == 0) r = mix(2.0 * nl * sharp + nl * nl * (1.0 - 2.0 * sharp), 2.0 * nl * (1.0 - sharp) + sqrt(max(nl, 0.0)) * (2.0 * sharp - 1.0), step(0.49, sharp));
    else if (blendMode == 1) r = mix(2.0 * nl * sharp, 1.0 - 2.0 * (1.0 - nl) * (1.0 - sharp), step(0.50, nl));
    else if (blendMode == 2) r = mix(2.0 * nl * sharp, 1.0 - 2.0 * (1.0 - nl) * (1.0 - sharp), step(0.50, sharp));
    else if (blendMode == 3) r = clamp(2.0 * nl * sharp, 0.0, 1.0);
    else if (blendMode == 4) r = mix(2.0 * nl * sharp, nl / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    else r = clamp(nl + (sharp - 0.5), 0.0, 1.0);
    return (isHDR) ? r * norm : r;
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 e = decodeToLinear(centerColor.rgb);
    float lE = getLuma(e);

    if (lE <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    // HDR adaptation luminance: reference level for relative threshold scaling. SDR -> 1.0 (no-op). HDR -> tracks local luminance.
    // Floor avoids division blowup in shadows; ceiling avoids absurd thresholds in extreme highlights.
    float hdrNorm = (isHDR) ? clamp(lE, 0.18, 16.0) : 1.0;

    // phase 1: shared 3x3 grid fetch
    vec3 a = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2(-1,-1)).rgb);
    vec3 b = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2( 0,-1)).rgb);
    vec3 c = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2( 1,-1)).rgb);
    vec3 d = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 0)).rgb);
    vec3 f = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 0)).rgb);
    vec3 g = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 1)).rgb);
    vec3 h = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2( 0, 1)).rgb);
    vec3 i = decodeToLinear(textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 1)).rgb);

    float lA = getLuma(a); float lB = getLuma(b); float lC = getLuma(c);
    float lD = getLuma(d); float lF = getLuma(f);
    float lG = getLuma(g); float lH = getLuma(h); float lI = getLuma(i);

    vec3 eCenter = e;

    float crossAvg = (lB + lH + lD + lF) * 0.25;

    // phase 1.1: min/max, local contrast, and band-pass mask (moved from phase 3)
    vec3 mnRGB  = min(min(min(d,e),min(f,b)),h);
    vec3 mnRGB2 = min(min(min(mnRGB,a),min(g,c)),i);
    vec3 trueMnRGB = mnRGB2;
    mnRGB += mnRGB2;
    vec3 mxRGB  = max(max(max(d,e),max(f,b)),h);
    vec3 mxRGB2 = max(max(max(mxRGB,a),max(g,c)),i);
    vec3 trueMxRGB = mxRGB2;
    mxRGB += mxRGB2;
    float localMaxLuma = getLuma(trueMxRGB);
    float localMinLuma = getLuma(trueMnRGB);
    float localContrast = localMaxLuma - localMinLuma;
    float bpLow = 0.01 * hdrNorm;
    float bpFadeIn = 0.04 * hdrNorm;
    float bpHigh = bandPassWidth * hdrNorm;
    float bpFadeOut = 0.3 * hdrNorm;
    float lowFreqFade = smoothstep(bpLow, bpLow + bpFadeIn, localContrast);
    float highFreqFade = 1.0 - smoothstep(bpHigh, bpHigh + bpFadeOut, localContrast);
    float bandPassMask = lowFreqFade * highFreqFade;

    // phase 1.2: full edge detection, edge mask, and micro texture mask (moved from phase 4)
    float crossMaxSM = max(lH, lE);
    float crossMinSM = min(lH, lE);
    float crossMaxESM = max(lF, crossMaxSM);
    float crossMinESM = min(lF, crossMinSM);
    float crossMaxWN = max(lB, lD);
    float crossMinWN = min(lB, lD);
    float crossRangeMax = max(crossMaxWN, crossMaxESM);
    float crossRangeMin = min(crossMinWN, crossMinESM);
    float crossRange = crossRangeMax - crossRangeMin;
    float fxaaThreshMin = fxaaEdgeThresholdMin * hdrNorm;
    float rangeMaxClamped = max(fxaaThreshMin, crossRangeMax * fxaaEdgeThreshold);
    bool earlyExit = crossRange < rangeMaxClamped;
    float lumaEdgeD1 = abs(lA + lI - 2.0 * lE);
    float lumaEdgeD2 = abs(lC + lG - 2.0 * lE);
    float edgeHorz1 = (lB + lH) - 2.0 * lE;
    float edgeVert1 = (lD + lF) - 2.0 * lE;
    float pureLumaEdgeH = abs(edgeHorz1);
    float pureLumaEdgeV = abs(edgeVert1);
    float lumaDiagRatio = min(pureLumaEdgeH, pureLumaEdgeV) / max(max(pureLumaEdgeH, pureLumaEdgeV), 0.0001);
    float edgeHorz2 = (-2.0 * lF) + (lC + lI);
    float edgeVert2 = (-2.0 * lB) + (lA + lC);
    float edgeHorz3 = (-2.0 * lD) + (lA + lG);
    float edgeVert3 = (-2.0 * lH) + (lG + lI);
    float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    float edgeH = abs(edgeHorz3) + edgeHorz4;
    float edgeV = abs(edgeVert3) + edgeVert4;
    vec3 edgeH_rgb = vec3(0.0);
    vec3 edgeV_rgb = vec3(0.0);
    if ((enableRGBEdgeDetection == 1 || enableFringeFix == 1) && qualityLevel == 0) {
        edgeH_rgb = abs(b + h - 2.0 * e);
        edgeV_rgb = abs(d + f - 2.0 * e);
        edgeH = max(edgeH_rgb.r, max(edgeH_rgb.g, edgeH_rgb.b));
        edgeV = max(edgeV_rgb.r, max(edgeV_rgb.g, edgeV_rgb.b));
        vec3 edgeD1_rgb = abs(a + i - 2.0 * e);
        vec3 edgeD2_rgb = abs(c + g - 2.0 * e);
        lumaEdgeD1 = max(edgeD1_rgb.r, max(edgeD1_rgb.g, edgeD1_rgb.b));
        lumaEdgeD2 = max(edgeD2_rgb.r, max(edgeD2_rgb.g, edgeD2_rgb.b));
    }
    float maxOrthoEdge = max(edgeH, edgeV);
    float maxDiag = max(lumaEdgeD1, lumaEdgeD2);
    float maxCombinedEdge = max(maxOrthoEdge, maxDiag);
    float edgeMask = 1.0 - smoothstep(rangeMaxClamped, rangeMaxClamped * 2.0, maxCombinedEdge);
    float isDiagonalEdge = smoothstep(0.6, 0.85, lumaDiagRatio);
    float totalEdgeEnergy = edgeH + edgeV + lumaEdgeD1 + lumaEdgeD2;
    float directionalPurity = maxCombinedEdge / max(totalEdgeEnergy, 0.0001);
    float effectivePurity = max(directionalPurity, isDiagonalEdge);
    float microTextureMask = smoothstep(0.25, 0.55, effectivePurity);
    bool isEdge = !earlyExit && (maxCombinedEdge > (fxaaEdgeThreshold * 0.5 * hdrNorm));

    // phase 1.3: local saturation from source center pixel (moved from phase 7)
    float localSaturation = max(max(e.r, e.g), e.b) - min(min(e.r, e.g), e.b);

    // phase 1.4: shared values for source corrections
    float maxNeighborLuma = max(max(lB, lH), max(lD, lF));
    float minNeighborLuma = min(min(lB, lH), min(lD, lF));
    float neighborSpread = maxNeighborLuma - minNeighborLuma;
    vec3 crossAvgRGB = (b + d + f + h) * 0.25;

    // phase 1.5 Checkerboard correction and anti speckle
    if ((enableDespeckle == 1 || enableCheckerboardFix == 1) && qualityLevel <= 2) {

        // Checkerboard transparency correction (structured alternating pattern)
        if (enableCheckerboardFix == 1) {
            float isolation    = abs(lE - crossAvg);
            float crossUniform = 1.0 - smoothstep(0.04 * hdrNorm, 0.15 * hdrNorm, neighborSpread);

            vec3 diagAvgRGB   = (a + c + g + i) * 0.25;
            float diagAgree   = 1.0 - smoothstep(0.03 * hdrNorm, 0.12 * hdrNorm, abs(lE - getLuma(diagAvgRGB)));
            float crossMismatch = smoothstep(0.04 * hdrNorm, 0.15 * hdrNorm, isolation);

            float checkerMask = diagAgree * crossMismatch * crossUniform;
            e  = mix(e, crossAvgRGB, checkerMask * checkerboardStrength);
            lE = getLuma(e);
        }

        // Impulse noise despeckle (random isolated outliers)
        if (enableDespeckle == 1) {
            float isolation     = abs(lE - crossAvg); // recomputed if checkerboard modified lE
            float neighborAgree = 1.0 - smoothstep(0.05 * hdrNorm, 0.2 * hdrNorm, neighborSpread);
            float outlierMask   = smoothstep(despeckleThreshold * hdrNorm, despeckleThreshold * 2.5 * hdrNorm, isolation) * neighborAgree;
            float targetLuma    = mix(lE, crossAvg, outlierMask);
            e  = e * (targetLuma / max(lE, 0.0001));
            lE = targetLuma;
        }
    }

    // phase 1.6: BC1/DXT1 green/magenta bias correction on the source. BC1 encodes green with 6 bits vs 5 bits for R/B.
    // creating green/magenta bias in low-saturation regions. Gated by saturation + neighbor agreement to avoid touching intentional green content.
    // Detects artifacts by measuring green bias disagreement between center and neighbors as BC1 artifacts cause inconsistent bias within 4x4 block
    if (enableBC1Fix == 1 && qualityLevel <= 2) {
        float bc1Sat = max(max(e.r, e.g), e.b) - min(min(e.r, e.g), e.b);
        if (bc1Sat < 0.08 * hdrNorm) {
            float bc1SatGate = 1.0 - smoothstep(0.02 * hdrNorm, 0.08 * hdrNorm, bc1Sat);

            // Bias disagreement: BC1 artifacts cause the center pixel's green bias to
            // differ from its neighbors. Intentional color has uniform bias across the area.
            float centerBias = e.g - (e.r + e.b) * 0.5;
            float neighborBias = ((b.g + d.g + f.g + h.g)
                - (b.r + d.r + f.r + h.r + b.b + d.b + f.b + h.b) * 0.5) * 0.25;
            float biasDiff = abs(centerBias - neighborBias);
            float bc1ArtifactMask = smoothstep(0.003 * hdrNorm, 0.015 * hdrNorm, biasDiff);

            float rbAvg = (e.r + e.b) * 0.5;
            float greenBias = e.g - rbAvg;
            float correction = clamp(greenBias,
                -bc1FixStrength * 0.03 * hdrNorm,
                 bc1FixStrength * 0.03 * hdrNorm);
            e.g -= correction * bc1SatGate * bc1ArtifactMask * guardStrength;
            lE = getLuma(e);
        }
    }

    // phase 1.7: Edge-Aware Chroma Smoothing on source (color denoise).
    // Kills TAA/compression color noise BEFORE sharpening amplifies it.
    if (enableChromaSmooth == 1) {
        float cross_luma = getNeutralLuma(crossAvgRGB);
        float center_luma = getNeutralLuma(e);
        float flatMask = 1.0 - smoothstep(0.05 * hdrNorm, 0.25 * hdrNorm, localContrast);
        float smoothAmount = flatMask * edgeMask * chromaSmoothStrength;
        // BC1/DXT1 blocks are always 4x4 aligned to gl_FragCoord. Chroma discontinuities
        // are sharpest at block boundaries, so boost chroma smoothing there.
        if (enableBC1Fix == 1 && qualityLevel <= 2) {
            vec2 blockPos = mod(gl_FragCoord.xy, 4.0);
            float blockEdge = 1.0 - smoothstep(0.0, 1.5,
                min(min(blockPos.x, 3.0 - blockPos.x), min(blockPos.y, 3.0 - blockPos.y)));
            smoothAmount *= mix(1.0, 1.2, blockEdge * bc1FixStrength);
        }
        e = mix(e, crossAvgRGB + vec3(center_luma - cross_luma), smoothAmount);
        lE = getLuma(e);
    }

    // phase 1.8: Chromatic Aberration (fringe) correction on source.
    // Detects per-channel edge disagreement and desaturates fringing zones BEFORE
    // sharpening amplifies them.
    if (enableFringeFix == 1 && qualityLevel == 0) {
        float hMax = max(edgeH_rgb.r, max(edgeH_rgb.g, edgeH_rgb.b));
        float hMin = min(edgeH_rgb.r, min(edgeH_rgb.g, edgeH_rgb.b));
        float vMax = max(edgeV_rgb.r, max(edgeV_rgb.g, edgeV_rgb.b));
        float vMin = min(edgeV_rgb.r, min(edgeV_rgb.g, edgeV_rgb.b));
        float fringe = max(hMax - hMin, vMax - vMin);
        float satGate = 1.0 - smoothstep(0.15 * hdrNorm, 0.4 * hdrNorm, localSaturation);
        float fringeMask = smoothstep(0.1 * hdrNorm, 0.3 * hdrNorm, fringe)
                        * fringeStrength * bandPassMask * satGate * edgeMask;
        float lumaHere = getNeutralLuma(e);
        e = mix(e, vec3(lumaHere), fringeMask);
        lE = getLuma(e);
    }

    // phase 1.9: Shimmer reduction on source (stabilizes isolated pixels).
    // Removes isolated flicker pixels BEFORE sharpening amplifies them.
    if (qualityLevel <= 3 && shimmerReduction > 0.0) {
        float isolation = abs(lE - crossAvg);
        float shimmerChoke = 1.0 - smoothstep(0.2 * hdrNorm, 0.4 * hdrNorm, localContrast);
        float shimmerMask = smoothstep(0.08 * hdrNorm, 0.2 * hdrNorm, isolation)
                        * microTextureMask * edgeMask
                        * shimmerChoke * shimmerReduction;
        if (shimmerMask > 0.0) {
            float clampedLuma = mix(lE, crossAvg, shimmerMask * 0.5);
            float shimmerScale = clampedLuma / max(lE, 0.0001);
            e *= shimmerScale;
            lE = clampedLuma;
        }
    }

    // phase 2: clarity wide fetches for latency hiding
    float h1_raw = getLuma(decodeToLinear(textureLod(img, textureCoord + vec2(pc.step1.x, 0.0), 0.0).rgb));
    float h2_raw = getLuma(decodeToLinear(textureLod(img, textureCoord - vec2(pc.step1.x, 0.0), 0.0).rgb));
    float v1_raw = getLuma(decodeToLinear(textureLod(img, textureCoord + vec2(0.0, pc.step1.y), 0.0).rgb));
    float v2_raw = getLuma(decodeToLinear(textureLod(img, textureCoord - vec2(0.0, pc.step1.y), 0.0).rgb));
    float h3_raw = 0.0; float h4_raw = 0.0; float v3_raw = 0.0; float v4_raw = 0.0;
    if (qualityLevel <= 1) {
        h3_raw = getLuma(decodeToLinear(textureLod(img, textureCoord + vec2(pc.step2.x, 0.0), 0.0).rgb));
        h4_raw = getLuma(decodeToLinear(textureLod(img, textureCoord - vec2(pc.step2.x, 0.0), 0.0).rgb));
        v3_raw = getLuma(decodeToLinear(textureLod(img, textureCoord + vec2(0.0, pc.step2.y), 0.0).rgb));
        v4_raw = getLuma(decodeToLinear(textureLod(img, textureCoord - vec2(0.0, pc.step2.y), 0.0).rgb));
    }

    // phase 3: CAS math (min/max, localContrast, bandPassMask moved to phase 1.1)
    vec3 ampRGB;
    if (isHDR) {
        ampRGB = clamp(mnRGB / max(mxRGB, 0.0001), 0.0, 1.0);
    } else {
        ampRGB = clamp(min(mnRGB, 2.0 - mxRGB) / max(mxRGB, 0.0001), 0.0, 1.0);
    }
    float peak = 8.0 - 3.0 * casSharpness;
    vec3 invAmp = inversesqrt(max(ampRGB, 0.0001));
    invAmp = min(invAmp, vec3(4.0));
    vec3 P = invAmp * peak;
    vec3 tightWindow = (b + d) + (f + h);
    vec3 casDeltaRGB = (4.0 * e - tightWindow) / (P - 4.0);
    // Phase 3.5: directional coherence gate to suppress amplification of compression artifacts (DCT ringing, oiliness etc.)
    float oilinessGate = 1.0;
    if (qualityLevel <= 2) {
        float invCenterLuma = 1.0 / max(lE, 0.01 * hdrNorm);
        float sobelX = ((lC + lF + lF + lI) - (lA + lD + lD + lG)) * invCenterLuma;
        float sobelY = ((lG + lH + lH + lI) - (lA + lB + lB + lC)) * invCenterLuma;
        float gradMagSq = sobelX * sobelX + sobelY * sobelY;
        float edgeThresholdSq = 0.04;
        float isDirectional = smoothstep(edgeThresholdSq, edgeThresholdSq * 4.0, gradMagSq);
        oilinessGate = mix(1.0, isDirectional, guardStrength);
    }

    // phase 5: FXAA Preset 39 and some extras.
    vec3 aaColor = e;

    if (isEdge && enableAA == 1) {
        bool isHorizontal = edgeH > edgeV;

        float subpixNSWE = lB + lH + lD + lF;
        float subpixNWSWNESE = lA + lC + lG + lI;
        float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
        float subpixB = subpixA * 0.08333333 - lE;
        float subpixRcpRange = 1.0 / max(crossRange, 0.0001);
        float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);
        float subpixD = (-2.0 * subpixC) + 3.0;
        float subpixE = subpixC * subpixC;
        float subpixF = subpixD * subpixE;
        float subpixG = subpixF * subpixF;
        float subpixH = subpixG * fxaaSubpixAmount;

        float lengthSign = 1.0;
        float lumaN_fxaa = isHorizontal ? lB : lD;
        float lumaS_fxaa = isHorizontal ? lH : lF;
        float gradientN = lumaN_fxaa - lE;
        float gradientS = lumaS_fxaa - lE;
        bool pairN = abs(gradientN) >= abs(gradientS);

        if (pairN) lengthSign = -1.0;

        float lumaNN = (pairN ? lumaN_fxaa : lumaS_fxaa) + lE;
        float gradient = max(abs(gradientN), abs(gradientS));
        float gradientScaled = gradient * 0.25;
        float lumaMM = lE - lumaNN * 0.5;
        bool lumaMLTZero = lumaMM < 0.0;

        vec2 posM = textureCoord;
        vec2 posB = posM;
        vec2 offNP = isHorizontal ? vec2(pc.pixelSize.x * fxaaSearchScale, 0.0) : vec2(0.0, pc.pixelSize.y * fxaaSearchScale);

        if (isHorizontal) posB.y += lengthSign * 0.5 * pc.pixelSize.y;
        else posB.x += lengthSign * 0.5 * pc.pixelSize.x;

        vec2 posN = posB - offNP * 1.0;
        vec2 posP = posB + offNP * 1.0;

        float halfLumaNN = lumaNN * 0.5;
        float lumaEndN = getLuma(decodeToLinear(textureLod(img, posN, 0.0).rgb)) - halfLumaNN;
        float lumaEndP = getLuma(decodeToLinear(textureLod(img, posP, 0.0).rgb)) - halfLumaNN;

        bool doneN = abs(lumaEndN) >= gradientScaled;
        bool doneP = abs(lumaEndP) >= gradientScaled;

        float steps[11] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

        for(int j = 0; j < 11; j++) {
            if (doneN && doneP) break;
            float stepSize = steps[j];
            vec2 step = offNP * stepSize;
            if (!doneN) {
                posN -= step;
                lumaEndN = getLuma(decodeToLinear(textureLod(img, posN, 0.0).rgb)) - halfLumaNN;
                doneN = abs(lumaEndN) >= gradientScaled;
            }
            if (!doneP) {
                posP += step;
                lumaEndP = getLuma(decodeToLinear(textureLod(img, posP, 0.0).rgb)) - halfLumaNN;
                doneP = abs(lumaEndP) >= gradientScaled;
            }
        }

        float dstN = isHorizontal ? abs(posM.x - posN.x) : abs(posM.y - posN.y);
        float dstP = isHorizontal ? abs(posP.x - posM.x) : abs(posP.y - posM.y);

        bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
        bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
        bool directionN = dstN < dstP;
        bool goodSpan = directionN ? goodSpanN : goodSpanP;

        float spanLength = dstN + dstP;
        float spanLengthRcp = 1.0 / max(spanLength, 0.0001);
        float dst = min(dstN, dstP);

        float pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
        float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
        float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);

        float thinLineMask = smoothstep(0.7, 0.95, subpixE);
        float finalShift = mix(pixelOffsetSubpix, 0.0, thinLineMask);

        float diagVectorWeight = smoothstep(0.85, 0.98, lumaDiagRatio);
        float sharpShift = clamp(finalShift * 1.25, -0.5, 0.5);
        finalShift = mix(finalShift, sharpShift, diagVectorWeight);

        finalShift = clamp(finalShift * lengthSign, -0.5, 0.5);

        vec2 perpOffset = isHorizontal ? vec2(0.0, pc.pixelSize.y) : vec2(pc.pixelSize.x, 0.0);
        vec2 finalUV = posM + finalShift * perpOffset;

        aaColor = decodeToLinear(textureLod(img, finalUV, 0.0).rgb);
    }

    // phase 6: clarity bilateral deltas and weights with 3x3 anchor
    // Perceptual luma: sharpening context
    float lumaAA = getLuma(aaColor);

    float bilateralThreshLow = edgeThreshLow * hdrNorm;
    float bilateralThreshHighBase = edgeThreshHigh * hdrNorm;

    float edgeChoke = smoothstep(0.15 * hdrNorm, 0.45 * hdrNorm, localContrast);
    float dynamicThreshHigh = mix(bilateralThreshHighBase, bilateralThreshLow + 0.02 * hdrNorm, edgeChoke);

    float invThreshRange = 1.0 / max(dynamicThreshHigh - bilateralThreshLow, 0.0001);
    // 3x3 grid diffs (always active, weight sum = 12)
    float diff = (
        bilateralDiff(lumaAA - lB, 2.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lD, 2.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lF, 2.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lH, 2.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lA, 1.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lC, 1.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lG, 1.0, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - lI, 1.0, bilateralThreshLow, invThreshRange)
    ) * (qualityLevel >= 2 ? 0.0714 : 0.0625);

    // Step1 wide diffs
    diff += (
        bilateralDiff(lumaAA - h1_raw, 0.5, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - h2_raw, 0.5, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - v1_raw, 0.5, bilateralThreshLow, invThreshRange) +
        bilateralDiff(lumaAA - v2_raw, 0.5, bilateralThreshLow, invThreshRange)
    ) * (qualityLevel >= 2 ? 0.0714 : 0.0625);

    // Step2 wide diffs (Perfect/Ultra only)
    if (qualityLevel <= 1) {
        // suppress step2 when wide samples form a smooth monotonic gradient to reduce long range banding artifacts.
        float hSpan = abs(h4_raw - h3_raw);
        float vSpan = abs(v4_raw - v3_raw);
        float gradientCoherence = smoothstep(0.01 * hdrNorm, 0.06 * hdrNorm, max(hSpan, vSpan));

        diff += (
            bilateralDiff(lumaAA - h3_raw, 0.5, bilateralThreshLow, invThreshRange) +
            bilateralDiff(lumaAA - h4_raw, 0.5, bilateralThreshLow, invThreshRange) +
            bilateralDiff(lumaAA - v3_raw, 0.5, bilateralThreshLow, invThreshRange) +
            bilateralDiff(lumaAA - v4_raw, 0.5, bilateralThreshLow, invThreshRange)
        ) * 0.0625 * mix(1.0, gradientCoherence, guardStrength);
    }

    if (localContrastStrength > 0.0 && qualityLevel <= 1) {
        float wideAvg = (h1_raw + h2_raw + h3_raw + h4_raw + v1_raw + v2_raw + v3_raw + v4_raw) * 0.125;
        // Halo prevention by clamping wide blur to the 3x3 bounds already computed by CAS.
        wideAvg = clamp(wideAvg, localMinLuma, localMaxLuma);
        float lcRatio = lumaAA / max(wideAvg, 0.0001 * hdrNorm);
        float lcBoost = pow(clamp(lcRatio, 0.25, 4.0), localContrastStrength) - 1.0;
        // BC1 fix: local contrast is the biggest amplifier of BC1 block-boundary luma steps.
        // Suppress it at 4px block edges in low-detail regions where the artifacts are visible.
        if (enableBC1Fix == 1 && qualityLevel <= 2) {
            vec2 blockPos = mod(gl_FragCoord.xy, 4.0);
            float blockEdge = 1.0 - smoothstep(0.0, 1.5,
                min(min(blockPos.x, 3.0 - blockPos.x), min(blockPos.y, 3.0 - blockPos.y)));
            float lowDetail = 1.0 - smoothstep(0.06 * hdrNorm, 0.2 * hdrNorm, localContrast);
            lcBoost *= mix(1.0, 1.0 - blockEdge * lowDetail, bc1FixStrength * guardStrength);
        }
        diff += lcBoost * 0.08 * hdrNorm * edgeMask;
    }

    // phase 7: clarity gates and s-curve with saturation and edge guards
    diff *= mix(1.0, bandPassMask, guardStrength);
    diff *= mix(1.0, mix(1.0 - clarityTextureProtection, 1.0, microTextureMask), guardStrength);
    diff *= mix(1.0, edgeMask, guardStrength);

    // localSaturation already computed in phase 1.3 from source center pixel
    if (qualityLevel <= 3) {
        float saturationGuard = 1.0 - smoothstep(0.4 * hdrNorm, 0.9 * hdrNorm, localSaturation);
        diff *= mix(1.0, saturationGuard, guardStrength);
    }

    float adjustedGuard = 1.0;
    if (qualityLevel <= 3) {
        float darkSmearGuard = (isHDR)
            ? (1.0 - smoothstep(0.0, 0.18, lumaAA))
            : (1.0 - min(lumaAA, 1.0));
        float brightnessContrast = maxNeighborLuma - lumaAA;    
        float edgeProximityGuard = 1.0 - smoothstep(0.0, 0.15 * hdrNorm, brightnessContrast);
        float combinedGuard = min(darkSmearGuard, edgeProximityGuard);
        adjustedGuard = mix(1.0, combinedGuard, guardStrength);
    }

    // Only apply positive bilateral boosts to directional detail. Darkening remains for shadow definition.
    diff = diff > 0.0 ? diff * oilinessGate : diff * adjustedGuard;

    float silhouetteGate = 1.0;
    if (qualityLevel <= 2) {
        float minCrossLuma = min(min(lB, lH), min(lD, lF));
        float silLow  = 0.005 * lE;
        float silHigh = 0.05  * lE;
        silhouetteGate = smoothstep(silLow, silHigh, minCrossLuma);
        diff *= mix(1.0, silhouetteGate, guardStrength);
    }

    // Extreme protection: At 0: no penalty at brightness extremes (sharpen everything equally).
    // At 1: full protection (old behavior). Default 0.5 is a middle ground.
    if (isHDR) {
        float shadowProtect = smoothstep(0.0, 0.18, lumaAA);
        diff *= mix(1.0, shadowProtect, extremeProtection);
    } else {
        float lumaAADev = lumaAA - 0.5;
        float distFromMidSq = lumaAADev * lumaAADev;
        float extremePenalty = clamp(1.0 - distFromMidSq * 2.5, 0.0, 1.0);
        diff *= mix(1.0, extremePenalty, extremeProtection);
    }

    // HDR: diff is naturally ~hdrNorm times larger in HDR. Scale the clamp.
    float maxDiffClamp = 0.15 * hdrNorm;
    diff = clamp(diff, -maxDiffClamp, maxDiffClamp);

    float blendMask = clamp(0.5 + diff / hdrNorm, 0.0, 1.0);
    // NEUTRAL luma for blend modes: preserves hue during saturation scaling
    float neutralLumaAA = getNeutralLuma(aaColor);
    float sharpLuma = applyBlendMode(neutralLumaAA, blendMask, hdrNorm);

    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = ((float(blendIfDark) / 255.0) + 0.0001) * hdrNorm;
        float blendIfL = ((float(blendIfLight) / 255.0) - 0.0001) * hdrNorm;
        float mask = 1.0;
        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, neutralLumaAA);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, neutralLumaAA);
        sharpLuma = mix(neutralLumaAA, sharpLuma, mask);
    }

    // phase 8: final composite & shimmer reduction
    vec3 finalColor;

    if (fxaaOnlyMode == 1) {
        finalColor = aaColor;

        if (enableAA == 1 && enableDebugAA == 1) {
            float intensity = clamp(length(aaColor - eCenter) * 8.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(1.0, 0.2, 0.2) * hdrNorm, intensity);
        }
    } else {
        float lumaScale = (neutralLumaAA > 0.0001) ? mix(neutralLumaAA, sharpLuma, clarityStrength) / neutralLumaAA : 1.0;
        vec3 clarityColor = aaColor * lumaScale;

        vec3 rgbRange = trueMxRGB - trueMnRGB;
        float maxChromaRange = max(max(rgbRange.r, rgbRange.g), rgbRange.b);

        float chromaNoise = (localContrast < 0.3 * hdrNorm) ? max(0.0, maxChromaRange - localContrast) : 0.0;
        float chromaPenalty = smoothstep(0.05 * hdrNorm, 0.2 * hdrNorm, chromaNoise);

        float invGuard = 1.0 - guardStrength;
        
        // Compute the scalar attenuation mask first (guarantees scalar ALU, not vector)
        float casMask = (1.0 - chromaPenalty * 0.8 * guardStrength)
                    * (invGuard + guardStrength * edgeMask)
                    * (invGuard + guardStrength * bandPassMask)
                    * (invGuard + guardStrength * silhouetteGate)
                    * oilinessGate;
                    
        vec3 casDeltaFinal = casDeltaRGB * casMask;

        float casScale = (isHDR) ? 0.5 : 1.0;
        finalColor = clarityColor + (casDeltaFinal * casStrength * casScale);
        float overshootBoost = 1.0 + (1.0 - guardStrength) * 0.5;

        float overshootScale = (isHDR) ? 0.5 : 1.0;
        vec3 overshoot = min(vec3(0.08 * hdrNorm * overshootBoost * overshootScale), max(vec3(0.03 * hdrNorm * overshootBoost * overshootScale), rgbRange * 0.15));

        finalColor = clamp(finalColor, trueMnRGB - overshoot, trueMxRGB + overshoot);

        // phase 10: Exposure (multiplicative gain in stops) + Brightness (additive lift)
        if (exposure != 0.0 || brightness != 0.0) {
            if (exposure != 0.0) {
                finalColor *= exp2(exposure);
            }
            if (brightness != 0.0) {
                finalColor += vec3(brightness * hdrNorm);
            }
        }

        // phase 10.2: Contrast (linear scale around mid) + S-Curve (hermite midtone push)
        if (contrast != 0.0 || sCurveStrength > 0.0) {
            vec3 t = finalColor / hdrNorm;
            vec3 shaped = t;
            
            vec3 inRange = step(0.0, t) * step(t, vec3(1.0));
            
            // S-Curve: built-in smoothstep handles the hermite polynomial and clamping internally. Only apply to [0, 1] range to preserve HDR highlights > 1.0.
            if (sCurveStrength > 0.0) {
                vec3 sCurve = smoothstep(0.0, 1.0, t);
                shaped = mix(shaped, mix(shaped, sCurve, sCurveStrength), inRange);
            }
            
            // Contrast: scale around 0.5
            if (contrast != 0.0) {
                shaped = (shaped - 0.5) * (1.0 + contrast) + 0.5;
            }
            
            // Soft clip: blend into hard clip at extremes for filmic shoulder. Reuses the invariant inRange mask to avoid crushing HDR highlights.
            vec3 hardClipped = clamp(shaped, 0.0, 1.0);
            vec3 softMask = smoothstep(0.8, 1.0, abs(shaped - 0.5) * 2.0);
            vec3 softClipped = mix(shaped, hardClipped, softMask);
            shaped = mix(shaped, softClipped, inRange);
            
            finalColor = max(shaped, 0.0) * hdrNorm;
        }

        // phase 10.4: Gamma / B/W tone response shaping. HDR aware.
        if (gammaAdjust != 0.0 || blackLift != 0.0 || whiteClip != 0.0) {
            vec3 graded = finalColor / hdrNorm;
            if (blackLift > 0.0) {
                graded = graded * (1.0 - blackLift) + blackLift;
            }
            if (whiteClip > 0.0) {
                graded = min(graded, vec3(1.0 - whiteClip));
            }
            if (gammaAdjust != 0.0) {
                graded = pow(max(graded, vec3(0.0)), vec3(1.0 / (1.0 + gammaAdjust)));
            }
            finalColor = graded * hdrNorm;
        }

        // phase 10.6: Temperature / Tint (white balance).
        if (temperature != 0.0 || tint != 0.0) {
            vec3 wb = vec3(1.0 + temperature, 1.0 + tint, 1.0 - temperature);
            wb /= getNeutralLuma(wb);
            finalColor *= wb;
        }

        // phase 10.8: CDL (per channel grade)
        if (enableCDL == 1) {
            vec3 cdlSlope  = vec3(cdlSlopeR, cdlSlopeG, cdlSlopeB);
            vec3 cdlOffset = vec3(cdlOffsetR, cdlOffsetG, cdlOffsetB);
            vec3 cdlPower  = vec3(cdlPowerR, cdlPowerG, cdlPowerB);
            vec3 cdl = finalColor / hdrNorm;
            cdl = cdl * cdlSlope + cdlOffset;
            cdl = max(cdl, 0.0);
            cdl = pow(cdl, cdlPower);
            finalColor = cdl * hdrNorm;
        }

        // phase 10.9: Vibrance + Saturation (merged chroma pass)
        if (vibrance != 0.0 || saturation != 0.0) {
            float max_c = max(finalColor.r, max(finalColor.g, finalColor.b));
            float min_c = min(finalColor.r, min(finalColor.g, finalColor.b));
            float chroma = max_c - min_c;
            if (chroma > 0.0001) {
                float sat_factor = 1.0;
                if (vibrance != 0.0) {
                    float sat_rel = chroma / max(max_c, 0.0001);
                    sat_factor += vibrance * (1.0 - clamp(sat_rel, 0.0, 1.0));
                }
                if (saturation != 0.0) {
                    sat_factor += saturation;
                }
                float new_chroma = clamp(chroma * sat_factor, 0.0, max_c);
                vec3 pure_chroma = finalColor - min_c;
                pure_chroma *= (new_chroma / chroma);
                float new_min_c = max_c - new_chroma;
                finalColor = pure_chroma + new_min_c;
            }
        }

        // phase 11: Split toning cinematic shadow/highlight tint.
        if (enableSplitTone == 1) {
            float stLuma = getNeutralLuma(finalColor) / hdrNorm;
            float shadowWeight    = 1.0 - smoothstep(0.0, 0.5, stLuma);
            float highlightWeight = smoothstep(0.5, 1.0, stLuma);
            vec3 shadowTint = vec3(stShadowR, stShadowG, stShadowB);
            shadowTint -= getNeutralLuma(shadowTint);
            vec3 highlightTint = vec3(stHighR, stHighG, stHighB);
            highlightTint -= getNeutralLuma(highlightTint);
            vec3 tint = shadowTint * shadowWeight + highlightTint * highlightWeight;
            finalColor += tint * splitToneStrength * hdrNorm;
        }

        // phase 11.5: Specular Highlight Desaturation.
        if (specularDesat != 0.0) {
            float max_spec = max(finalColor.r, max(finalColor.g, finalColor.b));
            float min_c = min(finalColor.r, min(finalColor.g, finalColor.b));
            float chroma = max_spec - min_c;
            if (chroma > 0.0001) {
                // Brightness gate: only very bright pixels (raised from 0.85 to 0.95)
                float spec_mask = smoothstep(0.95 * hdrNorm, 2.0 * hdrNorm, max_spec);
                // Localization gate: specular highlights stand out from neighbors (high local contrast). Flat bright surfaces have low local contrast and are skipped.
                float localization = smoothstep(0.08 * hdrNorm, 0.25 * hdrNorm, localContrast);
                spec_mask *= localization;
                float desat_factor = 1.0 - (spec_mask * specularDesat);
                float new_chroma = chroma * desat_factor;
                vec3 pure_chroma = finalColor - min_c;
                pure_chroma *= (new_chroma / chroma);
                float new_min_c = max_spec - new_chroma;
                finalColor = pure_chroma + new_min_c;
            }
        }

        float finalLuma = getLuma(finalColor);

        // phase 13: Filmic tone curve / highlight rolloff
        if (toneCurve != 0.0) {
            float knee = (isHDR) ? hdrNorm : 0.9;
            if (finalLuma > knee) {
                float excess = finalLuma - knee;
                // Rational soft-clip: smoothly compresses the excess above the knee
                float compressed = excess / (1.0 + (excess / knee) * toneCurve);
                finalColor *= (knee + compressed) / finalLuma;
                finalLuma = knee + compressed;
            }
        }

        // phase 14: shared noise generation (reused by deband, film grain, and dither)
        float noise = 0.0;
        if ((enableFilmGrain == 1 && qualityLevel <= 3) || enableDithering == 1 || enableDeband == 1) {
            // fine grain layer at 1:1 pixel resolution updating every frame
            uint fineSeed = uint(gl_FragCoord.x) * 747796405u
                          + uint(gl_FragCoord.y) * 2891336453u
                          + frameData.frameCounter * 2654435761u;

            uint fineHash = (fineSeed ^ (fineSeed >> 16)) * 0x85ebca6bu;
            fineHash = (fineHash ^ (fineHash >> 13)) * 0xc2b2ae35u;
            fineHash = fineHash ^ (fineHash >> 16);
            float fineNoise = (float(fineHash & 0xFFFFu) / 65535.0) * 2.0 - 1.0;

            // coarse grain layer at 1/4th resolution updating every 2 frames
            vec2 coarseCoord = floor(gl_FragCoord.xy * 0.25);
            uint coarseFrame = frameData.frameCounter / 2u;

            uint coarseSeed = uint(coarseCoord.x) * 1013904223u
                            + uint(coarseCoord.y) * 16807u
                            + coarseFrame * 48271u;

            uint coarseHash = (coarseSeed ^ (coarseSeed >> 16)) * 0x85ebca6bu;
            coarseHash = (coarseHash ^ (coarseHash >> 13)) * 0xc2b2ae35u;
            coarseHash = coarseHash ^ (coarseHash >> 16);
            float coarseNoise = (float(coarseHash & 0xFFFFu) / 65535.0) * 2.0 - 1.0;

            // hybrid blend of fine and coarse layers
            noise = (fineNoise * fineGrainWeight) + (coarseNoise * coarseGrainWeight);
        }

        // phase 15: Debanding breaks up color banding in flat/gradient regions by adding a small amount of the shared noise,
        // weighted toward low local-contrast areas where banding is visible, protected from real edges. Reuses localContrast, edgeMask, and noise.
        if (enableDeband == 1) {
            float flatMask = 1.0 - smoothstep(0.004 * hdrNorm, 0.025 * hdrNorm, localContrast);
            float debandMask = flatMask * edgeMask;
            // Direction aware debanding, when one edge axis dominates, boost amplitude we know where bands run. In ambiguity back off.
            float edgeTotal  = edgeH + edgeV;
            float dirClarity = max(edgeH, edgeV) / max(edgeTotal, 0.0001);
            // Amplitude sized near a quantization step to effectively break bands, deband step is calibrated for 8-bit SDR quantization. Cap at SDR white to prevent massive noise in HDR highlights.
            finalColor += vec3(noise) * debandMask * debandStrength * 0.004 * min(hdrNorm, 1.0) * mix(0.7, 1.3, dirClarity);
        }

        // phase 16: perceptual film grain
        float perceptualMask = 0.0;
        float finalGrainIntensity = 0.0;
    
        if (enableFilmGrain == 1 && qualityLevel <= 3) {
            // HDR: Normalize by hdrNorm so SDR white maps to 0.5 (peak of HVS curve), or scRGB SDR white at 1.0 hits the zero-crossing of 4*luma*(1-luma).
            float normalizedLuma = (isHDR) ? finalLuma / max(hdrNorm, 0.0001) : finalLuma;
            float lumaForGrain = clamp(normalizedLuma * 0.5, 0.0, 1.0);
            float hvsLumaWeight = 4.0 * lumaForGrain * (1.0 - lumaForGrain);

            float baseFloor = 0.15;

            float textureBoost = smoothstep(0.02 * hdrNorm, 0.12 * hdrNorm, localContrast) * 0.85;
            float spatialGrain = baseFloor + textureBoost;

            float edgeFade = 1.0 - smoothstep(0.0, rangeMaxClamped * 0.8, maxCombinedEdge);

            float clarityDelta = abs(sharpLuma - neutralLumaAA);
            float casDelta = length(casDeltaFinal);
            float sharpeningIntensity = max(clarityDelta, casDelta);

            float sharpeningFade = 1.0 - smoothstep(0.2 * hdrNorm, 0.8 * hdrNorm, sharpeningIntensity);

            perceptualMask = hvsLumaWeight * spatialGrain * edgeFade * sharpeningFade;

            float finalMask = max(perceptualMask, filmGrainMinimum * hvsLumaWeight);

            float grain = noise * finalMask * filmGrainStrength * 0.06 * min(hdrNorm, 1.0);
            finalColor += vec3(grain);
            finalGrainIntensity = abs(grain);
        }

        // phase 17: Contrast-adaptive dithering boosted in flat/low-contrast areas where banding is visible and reduced in textured areas where it's imperceptible. Reuses localContrast and shared noise.
        if (enableDithering == 1) {
            float flatBoost = 1.0 - smoothstep(0.01 * hdrNorm, 0.08 * hdrNorm, localContrast);
            float ditherScale = (isHDR) ? 0.15 : 1.0;
            float ditherAmp = 0.0019607843 * min(hdrNorm, 1.0) * mix(0.6, 1.6, flatBoost) * ditherScale;

            if (enableFilmGrain == 1) {
                float grainAmplitude = finalGrainIntensity;
                float ditherThreshold = 0.003 * hdrNorm;
                float ditherStrength = clamp((ditherThreshold - grainAmplitude) / ditherThreshold, 0.0, 1.0);
                finalColor += noise * ditherAmp * ditherStrength;
            } else {
                finalColor += noise * ditherAmp;
            }
        }

        // phase18: debug overlays
        if (enableAA == 1 && enableDebugAA == 1) {
            float intensity = clamp(length(aaColor - eCenter) * 8.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(1.0, 0.2, 0.2) * hdrNorm, intensity);
        }

        if (enableDebugCAS == 1) {
            float intensity = clamp(length(casDeltaFinal) * casStrength * 2.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(0.2, 1.0, 0.2) * hdrNorm, intensity);
        }

        if (enableDebugClarity == 1) {
            float clarityEffect = abs(sharpLuma - neutralLumaAA) * clarityStrength;
            float intensity = clamp(clarityEffect * 20.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(0.0, 0.8, 1.0) * hdrNorm, intensity);
        }

        if (enableFilmGrain == 1 && enableDebugGrain == 1) {
            float m = clamp(finalGrainIntensity * 25.0 / hdrNorm, 0.0, 1.0);

            vec3 heatLow = vec3(0.0, 0.0, 0.2);
            vec3 heatMid = vec3(0.0, 0.8, 0.9);
            vec3 heatHigh = vec3(1.0, 1.0, 0.0);

            vec3 heatColor = mix(heatLow, heatMid, smoothstep(0.0, 0.5, m));
            heatColor = mix(heatColor, heatHigh, smoothstep(0.5, 1.0, m));

            finalColor = mix(finalColor, heatColor * hdrNorm, 0.85);
        }
    }

    // Encode back to target color space, then clamp appropriately
    vec3 encodedColor = encodeFromLinear(finalColor);
    vec3 outColor = (isHDR) ? max(encodedColor, 0.0) : clamp(encodedColor, 0.0, 1.0);
    fragColor = vec4(outColor, centerColor.a);
}
