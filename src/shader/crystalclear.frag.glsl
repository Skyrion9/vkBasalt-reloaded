#version 450

// crystalclear: the "DLSS killer" :D spatial filter that brings out detail, 3d depth and gamma-respecting contrast with least amount of artifacting.
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
layout(constant_id = 29) const int hdrMode = 0;
layout(constant_id = 30) const float guardStrength = 0.6;
layout(constant_id = 31) const float bandPassWidth = 0.8;
layout(constant_id = 32) const float extremeProtection = 0.5;
layout(constant_id = 33) const float shimmerReduction = 0.5;


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

// 'bilateralThreshLow' and 'dynamicThreshHigh' are locals declared in main() before use, so the macros pick up the HDR-scaled thresholds.
#define BILATERAL_WEIGHT(diff) (1.0 - smoothstep(bilateralThreshLow, dynamicThreshHigh, abs(diff)))
#define BILATERAL_DIFF(neighbor, weight) ((lumaAA - neighbor) * BILATERAL_WEIGHT(lumaAA - neighbor) * weight)

float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

// HDR: 'norm' is the adaptation factor (hdrNorm). Mode 5 (Linear Light) is additive, self-normalizing via the downstream luma ratio, so it stays on raw values.
// The other modes are evaluated on normalized luma in HDR so their 1.0/clamp/step reference points remain valid above SDR white.
float applyBlendMode(float luma, float sharp, float norm) {
    if (blendMode == 5) return luma + 2.0 * sharp - 1.0;

    float nl = (hdrMode == 1) ? luma / norm : luma;
    float r;
    if (blendMode == 0) r = mix(2.0 * nl * sharp + nl * nl * (1.0 - 2.0 * sharp), 2.0 * nl * (1.0 - sharp) + sqrt(max(nl, 0.0)) * (2.0 * sharp - 1.0), step(0.49, sharp));
    else if (blendMode == 1) r = mix(2.0 * nl * sharp, 1.0 - 2.0 * (1.0 - nl) * (1.0 - sharp), step(0.50, nl));
    else if (blendMode == 2) r = mix(2.0 * nl * sharp, 1.0 - 2.0 * (1.0 - nl) * (1.0 - sharp), step(0.50, sharp));
    else if (blendMode == 3) r = clamp(2.0 * nl * sharp, 0.0, 1.0);
    else if (blendMode == 4) r = mix(2.0 * nl * sharp, nl / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    else r = clamp(nl + (sharp - 0.5), 0.0, 1.0);
    return (hdrMode == 1) ? r * norm : r;
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 e = centerColor.rgb;
    float lE = getLuma(e);

    if (lE <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    // HDR adaptation luminance: reference level for relative threshold scaling. SDR -> 1.0 (no-op). HDR -> tracks local luminance. 
    // Floor avoids division blowup in shadows; ceiling avoids absurd thresholds in extreme highlights.
    float hdrNorm = (hdrMode == 1) ? clamp(lE, 0.18, 16.0) : 1.0;

    // phase 1: shared 3x3 grid fetch
    vec3 a = textureLodOffset(img, textureCoord, 0.0, ivec2(-1,-1)).rgb;
    vec3 b = textureLodOffset(img, textureCoord, 0.0, ivec2( 0,-1)).rgb;
    vec3 c = textureLodOffset(img, textureCoord, 0.0, ivec2( 1,-1)).rgb;
    vec3 d = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 0)).rgb;
    vec3 f = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 0)).rgb;
    vec3 g = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 1)).rgb;
    vec3 h = textureLodOffset(img, textureCoord, 0.0, ivec2( 0, 1)).rgb;
    vec3 i = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 1)).rgb;

    float lA = getLuma(a); float lB = getLuma(b); float lC = getLuma(c);
    float lD = getLuma(d); float lF = getLuma(f);
    float lG = getLuma(g); float lH = getLuma(h); float lI = getLuma(i);

    // phase 2: clarity wide fetches for latency hiding
    float h1_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h2_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h3_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step2.x, 0.0), 0.0).rgb);
    float h4_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step2.x, 0.0), 0.0).rgb);

    float v1_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step1.y), 0.0).rgb);
    float v2_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step1.y), 0.0).rgb);
    float v3_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step2.y), 0.0).rgb);
    float v4_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step2.y), 0.0).rgb);

    // phase 3: "lantern" data, cas math and band-pass mask
    vec3 mnRGB  = min(min(min(d,e),min(f,b)),h);
    vec3 mnRGB2 = min(min(min(mnRGB,a),min(g,c)),i);
    vec3 trueMnRGB = mnRGB2;
    mnRGB += mnRGB2;

    vec3 mxRGB  = max(max(max(d,e),max(f,b)),h);
    vec3 mxRGB2 = max(max(max(mxRGB,a),max(g,c)),i);
    vec3 trueMxRGB = mxRGB2;
    mxRGB += mxRGB2;

    // SDR formula uses absolute headroom (2.0 - mxRGB) which collapses to 0 when mxRGB > 2.0. Use a scale-invariant local-contrast ratio for HDR.
    vec3 ampRGB;
    if (hdrMode == 1) {
        ampRGB = clamp(mnRGB / max(mxRGB, 0.0001), 0.0, 1.0);
    } else {
        ampRGB = clamp(min(mnRGB, 2.0 - mxRGB) / max(mxRGB, 0.0001), 0.0, 1.0);
    }

    float peak = 8.0 - 3.0 * casSharpness;
    vec3 invAmp = inversesqrt(max(ampRGB, 0.0001));
    vec3 P = invAmp * peak;
    vec3 tightWindow = (b + d) + (f + h);
    vec3 casDeltaRGB = (4.0 * e - tightWindow) / (P - 4.0);

    float localContrast = getLuma(trueMxRGB) - getLuma(trueMnRGB);

    float bpLow = 0.01 * hdrNorm;
    float bpFadeIn = 0.04 * hdrNorm;
    float bpHigh = bandPassWidth * hdrNorm;
    float bpFadeOut = 0.3 * hdrNorm;
    float lowFreqFade = smoothstep(bpLow, bpLow + bpFadeIn, localContrast);
    float highFreqFade = 1.0 - smoothstep(bpHigh, bpHigh + bpFadeOut, localContrast);
    float bandPassMask = lowFreqFade * highFreqFade;

    // phase 4: edge detection and relative early exit
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
    float rangeMaxScaled = crossRangeMax * fxaaEdgeThreshold;
    float rangeMaxClamped = max(fxaaThreshMin, rangeMaxScaled);
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

    if (enableRGBEdgeDetection == 1) {
        vec3 edgeH_rgb = abs(b + h - 2.0 * e);
        vec3 edgeV_rgb = abs(d + f - 2.0 * e);
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
    bool isHorizontal = edgeH > edgeV;

    // phase 5: FXAA Preset 39 and some extras.
    vec3 aaColor = e;
    float invEdgeConf = 1.0;

    if (isEdge) {
        invEdgeConf = 1.0 - smoothstep(rangeMaxScaled, rangeMaxScaled * 2.0, maxCombinedEdge);

        if (enableAA == 1) {
            float subpixNSWE = lB + lH + lD + lF;
            float subpixNWSWNESE = lA + lC + lG + lI;
            float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
            float subpixB = (subpixA * (1.0 / 12.0)) - lE;
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
            vec2 offNP = isHorizontal ? vec2(pc.pixelSize.x, 0.0) : vec2(0.0, pc.pixelSize.y);

            if (isHorizontal) posB.y += lengthSign * 0.5 * pc.pixelSize.y;
            else posB.x += lengthSign * 0.5 * pc.pixelSize.x;

            vec2 posN = posB - offNP * 1.0;
            vec2 posP = posB + offNP * 1.0;

            float lumaEndN = getLuma(textureLod(img, posN, 0.0).rgb) - lumaNN * 0.5;
            float lumaEndP = getLuma(textureLod(img, posP, 0.0).rgb) - lumaNN * 0.5;

            bool doneN = abs(lumaEndN) >= gradientScaled;
            bool doneP = abs(lumaEndP) >= gradientScaled;

            float steps[11] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

            for(int j = 0; j < 11; j++) {
                if (doneN && doneP) break;
                float stepSize = steps[j];
                if (!doneN) {
                    posN -= offNP * stepSize;
                    lumaEndN = getLuma(textureLod(img, posN, 0.0).rgb) - lumaNN * 0.5;
                    doneN = abs(lumaEndN) >= gradientScaled;
                }
                if (!doneP) {
                    posP += offNP * stepSize;
                    lumaEndP = getLuma(textureLod(img, posP, 0.0).rgb) - lumaNN * 0.5;
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

            aaColor = textureLod(img, finalUV, 0.0).rgb;
        }
    }

    // phase 6: clarity bilateral deltas and weights with 3x3 anchor
    float lumaAA = getLuma(aaColor);

    float bilateralThreshLow = edgeThreshLow * hdrNorm;
    float bilateralThreshHighBase = edgeThreshHigh * hdrNorm;

    float edgeChoke = smoothstep(0.15 * hdrNorm, 0.45 * hdrNorm, localContrast);
    float dynamicThreshHigh = mix(bilateralThreshHighBase, bilateralThreshLow + 0.02 * hdrNorm, edgeChoke);

    float diff = (
        BILATERAL_DIFF(lB, 2.0) + BILATERAL_DIFF(lD, 2.0) + BILATERAL_DIFF(lF, 2.0) + BILATERAL_DIFF(lH, 2.0) +
        BILATERAL_DIFF(lA, 1.0) + BILATERAL_DIFF(lC, 1.0) + BILATERAL_DIFF(lG, 1.0) + BILATERAL_DIFF(lI, 1.0) +
        BILATERAL_DIFF(h1_raw, 0.5) + BILATERAL_DIFF(h2_raw, 0.5) + BILATERAL_DIFF(h3_raw, 0.5) + BILATERAL_DIFF(h4_raw, 0.5) +
        BILATERAL_DIFF(v1_raw, 0.5) + BILATERAL_DIFF(v2_raw, 0.5) + BILATERAL_DIFF(v3_raw, 0.5) + BILATERAL_DIFF(v4_raw, 0.5)
    ) / 16.0;

    // phase 7: clarity gates and s-curve with saturation and edge guards
    diff *= mix(1.0, bandPassMask, guardStrength);
    diff *= mix(1.0, mix(1.0 - clarityTextureProtection, 1.0, microTextureMask), guardStrength);
    diff *= mix(1.0, edgeMask, guardStrength);

    float maxCenterRGB = max(max(aaColor.r, aaColor.g), aaColor.b);
    float minCenterRGB = min(min(aaColor.r, aaColor.g), aaColor.b);
    float localSaturation = maxCenterRGB - minCenterRGB;

    float saturationGuard = 1.0 - smoothstep(0.4 * hdrNorm, 0.9 * hdrNorm, localSaturation);
    diff *= mix(1.0, saturationGuard, guardStrength);

    float darkSmearGuard = 1.0 - min(lumaAA, 1.0);
    float maxNeighborLuma = max(max(lB, lH), max(lD, lF));
    float brightnessContrast = maxNeighborLuma - lumaAA;

    float edgeProximityGuard = 1.0 - smoothstep(0.0, 0.15 * hdrNorm, brightnessContrast);
    float combinedGuard = min(darkSmearGuard, edgeProximityGuard);

    float adjustedGuard = mix(1.0, combinedGuard, guardStrength);
    diff = diff > 0.0 ? diff : diff * adjustedGuard;

    // Extreme protection: At 0: no penalty at brightness extremes (sharpen everything equally).
    // At 1: full protection (old behavior). Default 0.5 is a middle ground.
    if (hdrMode == 1) {
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

    float blendMask = clamp(0.5 + diff, 0.0, 1.0);
    float sharpLuma = applyBlendMode(lumaAA, blendMask, hdrNorm);

    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = ((float(blendIfDark) / 255.0) + 0.0001) * hdrNorm;
        float blendIfL = ((float(blendIfLight) / 255.0) - 0.0001) * hdrNorm;
        float mask = 1.0;
        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, lumaAA);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, lumaAA);
        sharpLuma = mix(lumaAA, sharpLuma, mask);
    }

    // phase 8: final composite & shimmer reduction
    vec3 finalColor;

    if (fxaaOnlyMode == 1) {
        finalColor = aaColor;

        if (enableAA == 1 && enableDebugAA == 1) {
            float intensity = clamp(length(aaColor - e) * 8.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(1.0, 0.2, 0.2) * hdrNorm, intensity);
        }
    } else {
        float lumaScale = (lumaAA > 0.0001) ? mix(lumaAA, sharpLuma, clarityStrength) / lumaAA : 1.0;
        vec3 clarityColor = aaColor * lumaScale;

        vec3 rgbRange = trueMxRGB - trueMnRGB;
        float maxChromaRange = max(max(rgbRange.r, rgbRange.g), rgbRange.b);

        float chromaNoise = (localContrast < 0.3 * hdrNorm) ? max(0.0, maxChromaRange - localContrast) : 0.0;
        float chromaPenalty = smoothstep(0.05 * hdrNorm, 0.2 * hdrNorm, chromaNoise);

        vec3 casDeltaFinal = casDeltaRGB
            * (1.0 - chromaPenalty * 0.8 * guardStrength)
            * mix(1.0, edgeMask, guardStrength)
            * mix(1.0, bandPassMask, guardStrength);

        finalColor = clarityColor + (casDeltaFinal * casStrength);

        float overshootBoost = 1.0 + (1.0 - guardStrength) * 0.5;
        vec3 overshoot = min(vec3(0.08 * hdrNorm * overshootBoost), max(vec3(0.03 * hdrNorm * overshootBoost), rgbRange * 0.15));
        finalColor = clamp(finalColor, trueMnRGB - overshoot, trueMxRGB + overshoot);

        float finalLuma = getLuma(finalColor);

        float avgLuma = (lB + lH + lD + lF) * 0.25;
        float isolation = abs(lumaAA - avgLuma);

        float shimmerChoke = 1.0 - smoothstep(0.2 * hdrNorm, 0.4 * hdrNorm, localContrast);
        float shimmerMask = smoothstep(0.08 * hdrNorm, 0.2 * hdrNorm, isolation)
                          * microTextureMask * invEdgeConf * shimmerChoke * shimmerReduction;

        if (shimmerMask > 0.0) {
            float clampedLuma = mix(finalLuma, avgLuma, shimmerMask * 0.5);
            float shimmerScale = clampedLuma / max(finalLuma, 0.0001);
            finalColor *= shimmerScale;
            finalLuma *= shimmerScale;
        }

        // phase 9: unified noise generation for hybrid multi-scale procedural grain
        float noise = 0.0;
        if (enableFilmGrain == 1 || enableDithering == 1) {

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

        // tune to perception
        float perceptualMask = 0.0;
        float finalGrainIntensity = 0.0;

        if (enableFilmGrain == 1) {
            // HDR: Clamp luma for grain weight calculation so it doesn't invert on values > 1.0
            float lumaForGrain = (hdrMode == 1) ? clamp(finalLuma, 0.0, 1.0) : finalLuma;
            float hvsLumaWeight = 4.0 * lumaForGrain * (1.0 - lumaForGrain);

            float baseFloor = 0.15;

            float textureBoost = smoothstep(0.02 * hdrNorm, 0.12 * hdrNorm, localContrast) * 0.85;
            float spatialGrain = baseFloor + textureBoost;

            float edgeFade = 1.0 - smoothstep(0.0, rangeMaxClamped * 0.8, maxCombinedEdge);

            float clarityDelta = abs(sharpLuma - lumaAA);
            float casDelta = length(casDeltaFinal);
            float sharpeningIntensity = max(clarityDelta, casDelta);

            float sharpeningFade = 1.0 - smoothstep(0.2 * hdrNorm, 0.8 * hdrNorm, sharpeningIntensity);

            perceptualMask = hvsLumaWeight * spatialGrain * edgeFade * sharpeningFade;

            float finalMask = max(perceptualMask, filmGrainMinimum * hvsLumaWeight);

            float grain = noise * finalMask * filmGrainStrength * 0.06 * hdrNorm;
            finalColor += vec3(grain);
            finalGrainIntensity = abs(grain);
        }

        // phase 10: dithering for banding prevention that (can) complement grain
        if (enableDithering == 1) {
            float ditherAmp = 0.0019607843 * hdrNorm;
            if (enableFilmGrain == 1) {
                float grainAmplitude = finalGrainIntensity;
                float ditherThreshold = 0.003 * hdrNorm;
                float ditherStrength = clamp((ditherThreshold - grainAmplitude) / ditherThreshold, 0.0, 1.0);
                finalColor += noise * ditherAmp * ditherStrength;
            } else {
                finalColor += noise * ditherAmp;
            }
        }

        // phase11: debug overlays
        if (enableAA == 1 && enableDebugAA == 1) {
            float intensity = clamp(length(aaColor - e) * 8.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(1.0, 0.2, 0.2) * hdrNorm, intensity);
        }

        if (enableDebugCAS == 1) {
            float intensity = clamp(length(casDeltaFinal) * casStrength * 2.0 / hdrNorm, 0.0, 1.0);
            finalColor = mix(finalColor, vec3(0.2, 1.0, 0.2) * hdrNorm, intensity);
        }

        if (enableDebugClarity == 1) {
            float clarityEffect = abs(sharpLuma - lumaAA) * clarityStrength;
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

    // HDR: Preserve values > 1.0 for scRGB/HDR10, clamp for SDR
    vec3 outColor = (hdrMode == 1) ? max(finalColor, 0.0) : clamp(finalColor, 0.0, 1.0);
    fragColor = vec4(outColor, centerColor.a);
}
