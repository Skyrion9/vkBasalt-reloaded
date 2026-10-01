#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float sdrWhitePoint = 203.0;
layout(constant_id = 1) const float hdrPeakNits   = 1000.0;
layout(constant_id = 2) const int autoHdrEnabled  = 0;
layout(constant_id = 3) const int applyGain       = 1; // 0=passthrough (game calibrated), 1=apply manual/auto gain

layout(set = 1, binding = 0) readonly buffer AdaptiveMetrics
{
    float targetWhite;   // [0] For tone mapper
    float targetPeak;    // [1] For tone mapper
    float intensity;     // [2] For tone mapper
    float measuredWhite; // [3] For UI (unused in shader)
    float measuredPeak;  // [4] For UI (unused in shader)
    float padding1;
    float padding2;
    float padding3;
}
metrics;

#define HDR_TM_QUALITY 0
#define HDR_TM_FAST    1
#define HDR_TM_HERMITE 2

// Shifted down to avoid collision with colorSpaceMode (65535) from color_space.h
layout(constant_id = 65532) const int hdrToneMapperMode = HDR_TM_HERMITE;
layout(constant_id = 65533) const int sourceColorSpace  = CSP_SDR_SRGB;
layout(constant_id = 65534) const int destColorSpace    = CSP_SDR_SRGB;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

vec3 srgb_to_linear_fast(vec3 c)
{
    return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

vec3 decodeSource(vec3 c)
{
    if (sourceColorSpace == CSP_HDR10_PQ) return pq_to_linear(c);
    if (sourceColorSpace == CSP_HDR_HLG) return hlg_to_linear(c);
    if (sourceColorSpace == CSP_SDR_SRGB || sourceColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
        return (hdrToneMapperMode == HDR_TM_FAST) ? srgb_to_linear_fast(c) : srgb_to_linear(c);
    }
    return c;
}

vec3 encodeDest(vec3 c)
{
    if (destColorSpace == CSP_HDR10_PQ) return linear_to_pq(c);
    if (destColorSpace == CSP_HDR_HLG) return linear_to_hlg(c);
    if (destColorSpace == CSP_SDR_SRGB || destColorSpace == CSP_DISPLAY_P3_NONLINEAR) return linear_to_srgb(c);
    return c;
}

const float SDR_WHITE_LINEAR = 2.03; // 203 nits / 100

const mat3 MAT_709_TO_2020 = mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956);

const mat3 MAT_709_TO_P3 = mat3(0.8225, 0.0332, 0.0171, 0.1774, 0.9669, 0.0724, 0.0000, 0.0000, 0.9108);

const mat3 MAT_2020_TO_709 = mat3(1.6605, -0.1246, -0.0182, -0.5876, 1.1330, -0.1006, -0.0728, -0.0084, 1.1187);

const mat3 MAT_P3_TO_709 = mat3(1.2249, -0.0420, -0.0196, -0.2247, 1.0419, -0.0786, 0.0000, 0.0000, 1.0984);

const mat3 MAT_P3_TO_2020 = MAT_709_TO_2020 * MAT_P3_TO_709;
const mat3 MAT_2020_TO_P3 = MAT_709_TO_P3 * MAT_2020_TO_709;

vec3 maxRgbPeakClamp(vec3 c, float peakLinear)
{
    float maxC  = max(c.r, max(c.g, c.b));
    float scale = min(peakLinear / max(maxC, 0.0001), 1.0);
    return max(c * scale, vec3(0.0));
}

vec3 achromaticGamutClip(vec3 c, float mappedLuma, float peakLinear)
{
    float minC = min(c.r, min(c.g, c.b));
    float maxC = max(c.r, max(c.g, c.b));

    // Distance to peak: naturally >= 1.0 if in gamut, < 1.0 if out of gamut.
    float distToMax = (peakLinear - mappedLuma) / max(maxC - mappedLuma, 0.0001);
    // Distance to zero: naturally >= 1.0 if in gamut, < 1.0 if out of gamut.
    float distToMin = mappedLuma / max(mappedLuma - minC, 0.0001);
    // Take the most restrictive constraint and clamp to [0, 1]. If both distances are >= 1.0, clipFactor becomes 1.0 and the color is unchanged.
    float clipFactor = clamp(min(distToMax, distToMin), 0.0, 1.0);

    vec3 chroma = c - mappedLuma;
    return mappedLuma + chroma * clipFactor;
}

void main()
{
    vec4 raw    = textureLod(img, textureCoord, 0.0);
    vec3 linear = decodeSource(raw.rgb);

    // Gamut Mapping (Source Primaries -> Dest Primaries)
    int srcPrim = getPrimaries(sourceColorSpace);
    int dstPrim = getPrimaries(destColorSpace);
    if (srcPrim != dstPrim) {
        if (srcPrim == PRIMARIES_REC709 && dstPrim == PRIMARIES_REC2020)
            linear = MAT_709_TO_2020 * linear;
        else if (srcPrim == PRIMARIES_REC709 && dstPrim == PRIMARIES_P3)
            linear = MAT_709_TO_P3 * linear;
        else if (srcPrim == PRIMARIES_P3 && dstPrim == PRIMARIES_REC2020)
            linear = MAT_P3_TO_2020 * linear;
        else if (srcPrim == PRIMARIES_P3 && dstPrim == PRIMARIES_REC709)
            linear = MAT_P3_TO_709 * linear;
        else if (srcPrim == PRIMARIES_REC2020 && dstPrim == PRIMARIES_P3)
            linear = MAT_2020_TO_P3 * linear;
        else if (srcPrim == PRIMARIES_REC2020 && dstPrim == PRIMARIES_REC709)
            linear = MAT_2020_TO_709 * linear;
    }

    float luma       = dot(linear, getLumaCoeffs(destColorSpace));
    float mappedLuma = luma;

    if (autoHdrEnabled == 1 && sourceColorSpace == CSP_SDR_SRGB) {
        // AutoHDR path (SDR -> HDR). Source is SDR. 1.0 in linear space = SDR white.
        float targetWhite = metrics.targetWhite;
        float targetPeak  = metrics.targetPeak;

        if (hdrToneMapperMode == HDR_TM_QUALITY || hdrToneMapperMode == HDR_TM_HERMITE) {
            float satFactor = 1.0;

            // Specular weighting: achromatic pixels (light sources) expand to full peak, chromatic pixels (surfaces) expand to a lower ceiling.
            vec3 chromaVec         = linear - vec3(luma);
            float chromaLen        = length(chromaVec);
            float chromaRatio      = chromaLen / max(luma, 0.0001);
            float achromWeight     = 1.0 - clamp(chromaRatio, 0.0, 1.0);
            float chromaticCeiling = min(targetWhite * 3.0, targetPeak);
            float effectivePeak    = mix(chromaticCeiling, targetPeak, achromWeight);

            if (hdrToneMapperMode == HDR_TM_QUALITY) {
                if (luma <= 1.0) {
                    mappedLuma = luma * targetWhite;
                } else {
                    float x        = luma - 1.0;
                    float W        = targetWhite;
                    float headroom = effectivePeak - W;
                    float num      = x * headroom * W;
                    float den      = x * W + headroom;
                    mappedLuma     = W + num / max(den, 0.0001);
                }
            } else { // HDR_TM_HERMITE
                if (luma <= 1.0) {
                    float t    = luma;
                    float m0   = targetWhite;
                    float m1   = (targetWhite * (effectivePeak - targetWhite)) / max(effectivePeak, 0.0001);
                    float t2   = t * t;
                    float t3   = t2 * t;
                    float h10  = t3 - 2.0 * t2 + t;
                    float h01  = -2.0 * t3 + 3.0 * t2;
                    float h11  = t3 - t2;
                    mappedLuma = h10 * m0 + h01 * targetWhite + h11 * m1;
                } else {
                    float num  = luma * effectivePeak * targetWhite;
                    float den  = luma * targetWhite + (effectivePeak - targetWhite);
                    mappedLuma = num / max(den, 0.0001);
                }
            }

            float invSatRange = 1.0 / max(effectivePeak - targetWhite, 0.0001);
            float t           = clamp((mappedLuma - targetWhite) * invSatRange, 0.0, 1.0);
            satFactor         = 1.0 - 0.15 * (t * t * (3.0 - 2.0 * t));

            float expansionRatio = mappedLuma / max(luma, 0.0001);
            vec3 expandedChroma  = chromaVec * expansionRatio;
            linear               = mappedLuma + expandedChroma * satFactor;
            linear               = achromaticGamutClip(linear, mappedLuma, targetPeak);
        } else {
            // HDR_TM_FAST
            float expanded = luma * targetWhite;
            mappedLuma     = expanded - (expanded * expanded) / (4.0 * targetPeak);
            mappedLuma     = min(mappedLuma, targetPeak);

            float expansion = mappedLuma / max(luma, 0.0001);
            linear *= expansion;
            float satFactor = 1.0 - 0.10 * (luma * luma);
            linear          = mix(vec3(mappedLuma), linear, satFactor);
            linear          = maxRgbPeakClamp(linear, targetPeak);
        }
    } else {
        // Native HDR path (HDR -> HDR)
        if (applyGain == 1) {
            // Manual Calibration: Apply gain and scene-adaptive clipping
            float gain = metrics.targetWhite / SDR_WHITE_LINEAR;
            linear *= gain;
            mappedLuma = luma * gain;

            float peakLinear = metrics.targetPeak;

            if (hdrToneMapperMode == HDR_TM_QUALITY || hdrToneMapperMode == HDR_TM_HERMITE) {
                linear = achromaticGamutClip(linear, mappedLuma, peakLinear);
            } else {
                linear = maxRgbPeakClamp(linear, peakLinear);
            }
        } else {
            // Passthrough (Scene Analysis Only) The game is already calibrated. Don't do scene adaptive clipping,
            // as clipping to the scene's P99 would crush highlights to grayscale. Only apply a hard safety clamp to the display's physical peak.
            float physicalPeak = hdrPeakNits * 0.01; // Convert nits to linear space (1.0 = 100 nits)
            linear             = maxRgbPeakClamp(linear, physicalPeak);
            mappedLuma         = luma;
        }
    }

    vec3 encoded = encodeDest(linear);
    fragColor    = vec4(encoded, raw.a);
}
