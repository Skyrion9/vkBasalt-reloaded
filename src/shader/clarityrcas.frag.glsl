#version 450

// clarity rcas: hybrid macro-contrast and robust micro-sharpening
// comes with various artifacting protections, film grain.
// Recommend you use crystalclear instead. This is meant for lower-end systems which can't spare VRAM bandwidth.
// More prone to artifacts than crystalclear but can look sharper if you like the crunchy FSR look.

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float radius = 2.0;
layout(constant_id = 1) const float offset = 1.5;
layout(constant_id = 2) const float clarityStrength = 1.0;
layout(constant_id = 3) const int blendMode = 1;
layout(constant_id = 4) const int blendIfDark = 40;
layout(constant_id = 5) const int blendIfLight = 220;
layout(constant_id = 6) const float rcasSharpness = 1.0; 
layout(constant_id = 7) const float rcasStrength = 1.0; 
layout(constant_id = 8) const float edgeThreshLow = 0.05;
layout(constant_id = 9) const float edgeThreshHigh = 0.25;
layout(constant_id = 10) const int enableDithering = 1; 
layout(constant_id = 11) const int enableFilmGrain = 1;
layout(constant_id = 12) const float filmGrainStrength = 0.35;
layout(constant_id = 13) const float filmGrainMinimum = 0.0;
layout(constant_id = 14) const float fineGrainWeight = 0.6;
layout(constant_id = 15) const float coarseGrainWeight = 0.4;

layout(push_constant) uniform PushConstants {
    vec2 step1;     
    vec2 step2;     
} pc;

layout(set = 0, binding = 1) uniform FrameData {
    uint frameCounter;
} frameData;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

#define BILATERAL_WEIGHT(diff) (1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(diff)))
#define BILATERAL_DIFF(neighbor, weight) ((lE - neighbor) * BILATERAL_WEIGHT(lE - neighbor) * weight)

float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

float applyBlendMode(float luma, float sharp) {
    if (blendMode == 0) return mix(2.0 * luma * sharp + luma * luma * (1.0 - 2.0 * sharp), 2.0 * luma * (1.0 - sharp) + sqrt(luma) * (2.0 * sharp - 1.0), step(0.49, sharp));
    else if (blendMode == 1) return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, luma));
    else if (blendMode == 2) return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, sharp));
    else if (blendMode == 3) return clamp(2.0 * luma * sharp, 0.0, 1.0);
    else if (blendMode == 4) return mix(2.0 * luma * sharp, luma / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    else if (blendMode == 5) return luma + 2.0 * sharp - 1.0;
    else return clamp(luma + (sharp - 0.5), 0.0, 1.0); 
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 e = centerColor.rgb;
    float lE = getLuma(e);

    if (lE <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    // phase 1: rcas 5-tap cross fetches
    vec3 b = textureLodOffset(img, textureCoord, 0.0, ivec2( 0,-1)).rgb;
    vec3 d = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 0)).rgb;
    vec3 f = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 0)).rgb;
    vec3 h = textureLodOffset(img, textureCoord, 0.0, ivec2( 0, 1)).rgb;

    float lB = getLuma(b);
    float lD = getLuma(d);
    float lF = getLuma(f);
    float lH = getLuma(h);

    // phase 2: clarity wide fetches (early to hide latency)
    float h1_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h2_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h3_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step2.x, 0.0), 0.0).rgb);
    float h4_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step2.x, 0.0), 0.0).rgb);
    
    float v1_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step1.y), 0.0).rgb);
    float v2_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step1.y), 0.0).rgb);
    float v3_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step2.y), 0.0).rgb);
    float v4_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step2.y), 0.0).rgb);

    // phase 3: native rgb rcas math and local contrast
    vec3 mnRGB = min(min(b, d), min(f, h));
    mnRGB = min(mnRGB, e);
    
    vec3 mxRGB = max(max(b, d), max(f, h));
    mxRGB = max(mxRGB, e);

    float localContrast = getLuma(mxRGB) - getLuma(mnRGB);

    float lowFreqFade = smoothstep(0.01, 0.05, localContrast);
    float highFreqFade = 1.0 - smoothstep(0.35, 0.65, localContrast);
    float bandPassMask = lowFreqFade * highFreqFade;

    vec3 delta = 4.0 * e - (b + d + f + h);
    vec3 sharpDelta = delta * rcasSharpness; 
    vec3 safeDelta = clamp(sharpDelta, mnRGB - e, mxRGB - e);
    
    vec3 casDeltaRGB = safeDelta * rcasStrength * bandPassMask;

    // phase 4: bilateral delta accumulation (5-tap anchored)
    float diff = (
        BILATERAL_DIFF(lB, 2.0) + BILATERAL_DIFF(lD, 2.0) + BILATERAL_DIFF(lF, 2.0) + BILATERAL_DIFF(lH, 2.0) +
        BILATERAL_DIFF(h1_raw, 0.5) + BILATERAL_DIFF(h2_raw, 0.5) + BILATERAL_DIFF(v1_raw, 0.5) + BILATERAL_DIFF(v2_raw, 0.5) +
        BILATERAL_DIFF(h3_raw, 0.25) + BILATERAL_DIFF(h4_raw, 0.25) + BILATERAL_DIFF(v3_raw, 0.25) + BILATERAL_DIFF(v4_raw, 0.25)
    ) / 16.0;

    // phase 5: clarity gates and s-curve
    diff *= bandPassMask;

    float texturePenalty = smoothstep(0.4, 0.8, localContrast);
    diff *= (1.0 - texturePenalty * 0.5); 

    float maxCenterRGB = max(max(e.r, e.g), e.b);
    float minCenterRGB = min(min(e.r, e.g), e.b);
    float localSaturation = maxCenterRGB - minCenterRGB;
    float saturationGuard = 1.0 - smoothstep(0.4, 0.9, localSaturation);
    diff *= saturationGuard;

    float maxNeighborLuma = max(max(lB, lH), max(lD, lF));
    float brightnessContrast = maxNeighborLuma - lE;
    float edgeProximityGuard = 1.0 - smoothstep(0.0, 0.15, brightnessContrast);
    float darkSmearGuard = 1.0 - lE;
    float combinedGuard = min(darkSmearGuard, edgeProximityGuard);
    diff = diff > 0.0 ? diff : diff * combinedGuard;

    float lumaDev = lE - 0.5;
    float distFromMidSq = lumaDev * lumaDev;
    diff *= clamp(1.0 - distFromMidSq * 4.0, 0.0, 1.0); 

    diff = clamp(diff, -0.15, 0.15);

    float blendMask = clamp(0.5 + diff, 0.0, 1.0); 
    float sharpLuma = applyBlendMode(lE, blendMask);

    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = (float(blendIfDark) / 255.0) + 0.0001;
        float blendIfL = (float(blendIfLight) / 255.0) - 0.0001;
        float mask = 1.0;
        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, lE);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, lE);
        sharpLuma = mix(lE, sharpLuma, mask);
    }

    // phase 6: final composite
    float lumaScale = (lE > 0.0001) ? mix(lE, sharpLuma, clarityStrength) / lE : 1.0;
    vec3 clarityColor = e * lumaScale;

    vec3 rgbRange = mxRGB - mnRGB;
    float maxChromaRange = max(max(rgbRange.r, rgbRange.g), rgbRange.b);
    float chromaNoise = (localContrast < 0.3) ? max(0.0, maxChromaRange - localContrast) : 0.0;
    float chromaPenalty = smoothstep(0.05, 0.2, chromaNoise);
    casDeltaRGB *= (1.0 - chromaPenalty * 0.8);

    vec3 finalColor = clarityColor + casDeltaRGB;

    vec3 overshoot = min(vec3(0.08), max(vec3(0.03), rgbRange * 0.15));
    finalColor = clamp(finalColor, mnRGB - overshoot, mxRGB + overshoot);

    // phase 7: shimmer reduction isolation gate
    float avgLuma = (lB + lD + lF + lH) * 0.25;
    float isolation = abs(lE - avgLuma);
    
    float edgeProtection = 1.0 - smoothstep(0.15, 0.35, localContrast);
    float shimmerMask = smoothstep(0.05, 0.15, isolation) * (1.0 - texturePenalty) * edgeProtection;
    
    float finalLuma = getLuma(finalColor);
    if (shimmerMask > 0.0) {
        float clampedLuma = mix(finalLuma, avgLuma, shimmerMask * 0.5);
        float shimmerScale = clampedLuma / max(finalLuma, 0.0001);
        finalColor *= shimmerScale;
        finalLuma *= shimmerScale;
    }

    // phase 8: unified noise generation for hybrid multi-scale procedural grain
    float noise = 0.0;
    if (enableFilmGrain == 1 || enableDithering == 1) {
        uint fineSeed = uint(gl_FragCoord.x) * 747796405u 
                      + uint(gl_FragCoord.y) * 2891336453u 
                      + frameData.frameCounter * 2654435761u;
        
        uint fineHash = (fineSeed ^ (fineSeed >> 16)) * 0x85ebca6bu;
        fineHash = (fineHash ^ (fineHash >> 13)) * 0xc2b2ae35u;
        fineHash = fineHash ^ (fineHash >> 16);
        float fineNoise = (float(fineHash & 0xFFFFu) / 65535.0) * 2.0 - 1.0;

        vec2 coarseCoord = floor(gl_FragCoord.xy * 0.25);
        uint coarseFrame = frameData.frameCounter / 2u;
        
        uint coarseSeed = uint(coarseCoord.x) * 1013904223u 
                        + uint(coarseCoord.y) * 16807u 
                        + coarseFrame * 48271u;
                        
        uint coarseHash = (coarseSeed ^ (coarseSeed >> 16)) * 0x85ebca6bu;
        coarseHash = (coarseHash ^ (coarseHash >> 13)) * 0xc2b2ae35u;
        coarseHash = coarseHash ^ (coarseHash >> 16);
        float coarseNoise = (float(coarseHash & 0xFFFFu) / 65535.0) * 2.0 - 1.0;

        noise = (fineNoise * fineGrainWeight) + (coarseNoise * coarseGrainWeight);
    }

    // phase 9: perceptual film grain
    float finalGrainIntensity = 0.0;
    if (enableFilmGrain == 1) {
        float hvsLumaWeight = 4.0 * finalLuma * (1.0 - finalLuma);
        float baseFloor = 0.15; 
        float textureBoost = smoothstep(0.02, 0.12, localContrast) * 0.85; 
        float spatialGrain = baseFloor + textureBoost;
        
        float edgeFade = 1.0 - smoothstep(0.2, 0.5, localContrast);
        
        float clarityDelta = abs(sharpLuma - lE);
        float casDelta = length(casDeltaRGB); 
        float sharpeningIntensity = max(clarityDelta, casDelta);
        float sharpeningFade = 1.0 - smoothstep(0.2, 0.8, sharpeningIntensity);
        
        float perceptualMask = hvsLumaWeight * spatialGrain * edgeFade * sharpeningFade;
        float finalMask = max(perceptualMask, filmGrainMinimum * hvsLumaWeight);
        
        float grain = noise * finalMask * filmGrainStrength * 0.06;
        finalColor += vec3(grain);
        finalGrainIntensity = abs(grain);
    }

    // phase 10: dithering that complements grain
    if (enableDithering == 1) {
        if (enableFilmGrain == 1) {
            float grainAmplitude = finalGrainIntensity;
            float ditherThreshold = 0.003; 
            float ditherStrength = clamp((ditherThreshold - grainAmplitude) / ditherThreshold, 0.0, 1.0);
            finalColor += noise * 0.0019607843 * ditherStrength;
        } else {
            finalColor += noise * 0.0019607843; 
        }
    }

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), centerColor.a);
}

//##############P##############
//##############P##############
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//##############P##############
//##############P##############
//#############################
//##############C##############
//PP########PP#C0C#PP########PP
//##############C##############
//#############################
//##############P##############
//##############P##############
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//#############################
//##############P##############
//##############P##############
