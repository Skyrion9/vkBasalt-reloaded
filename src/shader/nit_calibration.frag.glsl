#version 450

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float sdrWhitePoint = 203.0;
layout(constant_id = 1) const float hdrPeakNits = 1000.0;
layout(constant_id = 2) const int autoHdrEnabled = 0;

#define CSP_SDR_SRGB              0
#define CSP_HDR10_PQ              1
#define CSP_HDR_HLG               2
#define CSP_HDR_SCRGB             3
#define CSP_HDR_BT2020_LINEAR     4
#define CSP_HDR_DISPLAY_P3_LINEAR 5
#define CSP_DISPLAY_P3_NONLINEAR  6

#define HDR_TM_QUALITY 0
#define HDR_TM_FAST    1
#define HDR_TM_HERMITE 2

layout(constant_id = 65533) const int hdrToneMapperMode = HDR_TM_HERMITE;
layout(constant_id = 65534) const int sourceColorSpace  = CSP_SDR_SRGB;
layout(constant_id = 65535) const int destColorSpace    = CSP_SDR_SRGB;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linear_to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

const float PQ_M1 = 0.1593017578125;
const float PQ_M2 = 78.84375;
const float PQ_C1 = 0.8359375;
const float PQ_C2 = 18.8515625;
const float PQ_C3 = 18.6875;

vec3 pq_to_linear(vec3 c) {
    vec3 N = pow(max(c, vec3(0.0)), vec3(1.0 / PQ_M2));
    vec3 linear = pow(max(N - PQ_C1, vec3(0.0)) / (PQ_C2 - PQ_C3 * N), vec3(1.0 / PQ_M1));
    return linear * 100.0;
}
vec3 linear_to_pq(vec3 c) {
    vec3 L = pow(max(c / 100.0, vec3(0.0)), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * L) / (vec3(1.0) + PQ_C3 * L), vec3(PQ_M2));
}

const float HLG_A = 0.17883277;
const float HLG_B = 0.28466892;
const float HLG_C = 0.55991073;

vec3 hlg_to_linear(vec3 c) {
    vec3 linear = mix(c * c / 3.0, (exp((c - HLG_C) / HLG_A) + HLG_B) / 12.0, step(vec3(0.5), c));
    return linear * 10.0;
}
vec3 linear_to_hlg(vec3 c) {
    vec3 L = max(c / 10.0, vec3(0.0));
    return mix(sqrt(3.0 * L), HLG_A * log(12.0 * L - HLG_B) + HLG_C, step(vec3(1.0 / 12.0), L));
}

vec3 srgb_to_linear_fast(vec3 c) {
    return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

vec3 decodeSource(vec3 c) {
    if (sourceColorSpace == CSP_HDR10_PQ) return pq_to_linear(c);
    if (sourceColorSpace == CSP_HDR_HLG)  return hlg_to_linear(c);
    if (sourceColorSpace == CSP_SDR_SRGB || sourceColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
        return (hdrToneMapperMode == HDR_TM_FAST) ? srgb_to_linear_fast(c) : srgb_to_linear(c);
    }
    return c;
}

vec3 encodeDest(vec3 c) {
    if (destColorSpace == CSP_HDR10_PQ) return linear_to_pq(c);
    if (destColorSpace == CSP_HDR_HLG)  return linear_to_hlg(c);
    if (destColorSpace == CSP_SDR_SRGB || destColorSpace == CSP_DISPLAY_P3_NONLINEAR) return linear_to_srgb(c);
    return c;
}

// Gamut & Luma
const vec3 LUMA_REC709  = vec3(0.2126, 0.7152, 0.0722);
const vec3 LUMA_REC2020 = vec3(0.2627, 0.6780, 0.0593);
const vec3 LUMA_P3      = vec3(0.2289, 0.6917, 0.0793);

const mat3 MAT_709_TO_2020 = mat3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0114, 0.8956
);

const mat3 MAT_709_TO_P3 = mat3(
    0.8225, 0.0332, 0.0171,
    0.1774, 0.9669, 0.0724,
    0.0000, 0.0000, 0.9108
);

const mat3 MAT_2020_TO_709 = mat3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1330, -0.1006,
    -0.0728, -0.0084,  1.1187
);

const mat3 MAT_P3_TO_709 = mat3(
     1.2249, -0.0420, -0.0196,
    -0.2247,  1.0419, -0.0786,
     0.0000,  0.0000,  1.0984
);

const mat3 MAT_P3_TO_2020 = MAT_709_TO_2020 * MAT_P3_TO_709;
const mat3 MAT_2020_TO_P3 = MAT_709_TO_P3 * MAT_2020_TO_709;

vec3 maxRgbPeakClamp(vec3 c, float peakLinear) {
    float maxC = max(c.r, max(c.g, c.b));
    float scale = min(peakLinear / max(maxC, 0.0001), 1.0);
    return max(c * scale, vec3(0.0));
}

vec3 achromaticGamutClip(vec3 c, float mappedLuma, float peakLinear) {
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

void main() {
    vec4 raw = textureLod(img, textureCoord, 0.0);
    vec3 linear = decodeSource(raw.rgb);

    // Exhaustive Gamut Mapping (Source Primaries -> Dest Primaries)
    if (sourceColorSpace == CSP_SDR_SRGB || sourceColorSpace == CSP_HDR_SCRGB) {
        if (destColorSpace == CSP_HDR10_PQ || destColorSpace == CSP_HDR_HLG || destColorSpace == CSP_HDR_BT2020_LINEAR) {
            linear = MAT_709_TO_2020 * linear;
        } else if (destColorSpace == CSP_HDR_DISPLAY_P3_LINEAR || destColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
            linear = MAT_709_TO_P3 * linear;
        }
    } else if (sourceColorSpace == CSP_HDR_DISPLAY_P3_LINEAR || sourceColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
        if (destColorSpace == CSP_HDR10_PQ || destColorSpace == CSP_HDR_HLG || destColorSpace == CSP_HDR_BT2020_LINEAR) {
            linear = MAT_P3_TO_2020 * linear;
        } else if (destColorSpace == CSP_SDR_SRGB || destColorSpace == CSP_HDR_SCRGB) {
            linear = MAT_P3_TO_709 * linear;
        }
    } else if (sourceColorSpace == CSP_HDR10_PQ || sourceColorSpace == CSP_HDR_HLG || sourceColorSpace == CSP_HDR_BT2020_LINEAR) {
        if (destColorSpace == CSP_HDR_DISPLAY_P3_LINEAR || destColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
            linear = MAT_2020_TO_P3 * linear;
        } else if (destColorSpace == CSP_SDR_SRGB || destColorSpace == CSP_HDR_SCRGB) {
            linear = MAT_2020_TO_709 * linear;
        }
    }

    vec3 lumaCoeffs = LUMA_REC709;
    if (destColorSpace == CSP_HDR10_PQ || destColorSpace == CSP_HDR_HLG || destColorSpace == CSP_HDR_BT2020_LINEAR) {
        lumaCoeffs = LUMA_REC2020;
    } else if (destColorSpace == CSP_HDR_DISPLAY_P3_LINEAR || destColorSpace == CSP_DISPLAY_P3_NONLINEAR) {
        lumaCoeffs = LUMA_P3;
    }
    float luma = dot(linear, lumaCoeffs);
    float mappedLuma = luma;

    if (hdrToneMapperMode == HDR_TM_QUALITY || hdrToneMapperMode == HDR_TM_HERMITE) {
        float satFactor = 1.0;
        if (autoHdrEnabled == 1 && sourceColorSpace == CSP_SDR_SRGB) {
            float targetWhite = sdrWhitePoint * 0.01;
            float targetPeak  = hdrPeakNits * 0.01;

            // Specular weighting: achromatic pixels (light sources) expand to full peak, chromatic pixels (surfaces) expand to a lower ceiling.
            vec3  chromaVec    = linear - vec3(luma);
            float chroma       = length(chromaVec);
            float chromaRatio  = chroma / max(luma, 0.0001);
            float achromWeight = 1.0 - clamp(chromaRatio, 0.0, 1.0);
            float chromaticCeiling = min(targetWhite * 3.0, targetPeak);
            float effectivePeak = mix(chromaticCeiling, targetPeak, achromWeight);

            float A = effectivePeak - 1.0; float C = 0.1; float D = 0.2; float E = 0.7;
            float num = fma(A, luma * luma, luma);
            float den = fma(C, luma * luma, fma(D, luma, E));
            mappedLuma = num / max(den, 0.0001);

            float invSatRange = 1.0 / max(effectivePeak - targetWhite, 0.0001);
            float t = clamp((mappedLuma - targetWhite) * invSatRange, 0.0, 1.0);
            satFactor = 1.0 - 0.15 * (t * t * (3.0 - 2.0 * t));
        } else {
            float targetWhite = sdrWhitePoint * 0.01;
            float targetPeak  = hdrPeakNits * 0.01;
            float knee = targetWhite;

            if (luma <= knee) {
                mappedLuma = luma * (targetWhite / knee);
            } else if (hdrToneMapperMode == HDR_TM_HERMITE) {
                // BT.2390-style cubic Hermite spline Maps [knee, sourceMax] -> [targetWhite, targetPeak] with C¹ continuity at the knee.
                // Tangent at knee matches the lower linear segment (slope = targetWhite/knee). Tangent at sourceMax is 0 (smooth roll-off to peak).
                float sourceMax = 100.0; // 10,000 nits (PQ absolute maximum)
                float dx = sourceMax - knee;
                float invRange = 1.0 / dx;
                float t = clamp((luma - knee) * invRange, 0.0, 1.0);
                float headroom = targetPeak - targetWhite;
                float slope = targetWhite / knee;
                float m0_dx = slope * dx;
                // Cubic Hermite in Horner form: ((a3*t + a2)*t + a1)*t + a0
                float a3 = -2.0 * headroom + m0_dx;
                float a2 =  3.0 * headroom - 2.0 * m0_dx;
                float a1 =  m0_dx;
                mappedLuma = ((a3 * t + a2) * t + a1) * t + targetWhite;
            } else {
                // Rational Reinhard with matched slope (C¹ continuous)
                float excess   = luma - knee;
                float headroom = targetPeak - targetWhite;
                float denom    = excess + (headroom * knee / targetWhite);
                mappedLuma     = targetWhite + (headroom * excess) / denom;
            }

            float invSatRange = 1.0 / max(targetPeak - targetWhite, 0.0001);
            float t = clamp((mappedLuma - targetWhite) * invSatRange, 0.0, 1.0);
            satFactor = 1.0 - 0.10 * (t * t * (3.0 - 2.0 * t));
        }
        // Scale chroma by the luminance expansion ratio to preserve chromaticity
        float expansionRatio = mappedLuma / max(luma, 0.0001);
        vec3 chroma = (linear - luma) * expansionRatio;

        // Apply Hunt effect compression to the expanded chroma
        linear = mappedLuma + chroma * satFactor;
        linear = achromaticGamutClip(linear, mappedLuma, hdrPeakNits / 100.0);

    } else {
        // HDR_TM_FAST
        if (autoHdrEnabled == 1 && sourceColorSpace == CSP_SDR_SRGB) {
            float targetWhite = sdrWhitePoint / 100.0;
            float targetPeak  = hdrPeakNits / 100.0;
            
            // Continuous quadratic: f(x) = x * ((peak - white)*x + white). Maps 0->0, slope at 0 is 'white' (preserves dark/midtone levels), 1->peak.
            float curve = fma(targetPeak - targetWhite, luma, targetWhite);
            mappedLuma = luma * curve;
            vec3 uniformlyScaled = linear * curve;
            
            // Fast Hunt effect approximation based on input luma
            float satFactor = 1.0 - 0.10 * (luma * luma);
            linear = mix(vec3(mappedLuma), uniformlyScaled, satFactor);
        } else {
            float gain = sdrWhitePoint / 203.0;
            linear *= gain;
            mappedLuma = luma * gain;
        }
        linear = maxRgbPeakClamp(linear, hdrPeakNits / 100.0);
    }

    vec3 encoded = encodeDest(linear);
    fragColor = vec4(encoded, raw.a);
}
