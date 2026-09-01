#ifndef COLOR_SPACE_GLSL
#define COLOR_SPACE_GLSL

#define CSP_SDR_SRGB              0
#define CSP_HDR10_PQ              1
#define CSP_HDR_HLG               2
#define CSP_HDR_SCRGB             3
#define CSP_HDR_BT2020_LINEAR     4
#define CSP_HDR_DISPLAY_P3_LINEAR 5
#define CSP_DISPLAY_P3_NONLINEAR  6

layout(constant_id = 65535) const int colorSpaceMode = CSP_SDR_SRGB;

// sRGB Transfer Functions
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linear_to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// ST 2084 (PQ) Transfer Functions
const float PQ_M1 = 0.1593017578125;
const float PQ_M2 = 78.84375;
const float PQ_C1 = 0.8359375;
const float PQ_C2 = 18.8515625;
const float PQ_C3 = 18.6875;

// PQ linear: 1.0 = 10,000 nits. Scale by 100 so 1.0 = SDR white (100 nits), matches the scRGB convention that hdrNorm thresholds are calibrated for.
vec3 pq_to_linear(vec3 c) {
    vec3 N = pow(max(c, vec3(0.0)), vec3(1.0 / PQ_M2));
    vec3 linear = pow(max(N - PQ_C1, vec3(0.0)) / (PQ_C2 - PQ_C3 * N), vec3(1.0 / PQ_M1));
    return linear * 100.0;
}
vec3 linear_to_pq(vec3 c) {
    vec3 L = pow(max(c / 100.0, vec3(0.0)), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * L) / (vec3(1.0) + PQ_C3 * L), vec3(PQ_M2));
}

// HLG (Hybrid Log-Gamma) Transfer Functions 
const float HLG_A = 0.17883277;
const float HLG_B = 0.28466892;
const float HLG_C = 0.55991073;

// HLG linear: 1.0 ≈ 1000 nits on a reference display. Scale by 10 so SDR white (~100 nits, scene linear ~0.1) maps to ~1.0.
vec3 hlg_to_linear(vec3 c) {
    vec3 linear = mix(c * c / 3.0, (exp((c - HLG_C) / HLG_A) + HLG_B) / 12.0, step(vec3(0.5), c));
    return linear * 10.0;
}
vec3 linear_to_hlg(vec3 c) {
    vec3 L = max(c / 10.0, vec3(0.0));
    return mix(sqrt(3.0 * L), HLG_A * log(12.0 * L - HLG_B) + HLG_C, step(vec3(1.0 / 12.0), L));
}

// Unified Decode/Encode Wrappers 
vec3 decodeToLinear(vec3 c) {
    if (colorSpaceMode == CSP_HDR10_PQ) return pq_to_linear(c);
    if (colorSpaceMode == CSP_HDR_HLG) return hlg_to_linear(c);
    if (colorSpaceMode == CSP_SDR_SRGB || colorSpaceMode == CSP_DISPLAY_P3_NONLINEAR) return srgb_to_linear(c);
    return c; // scRGB/BT2020/P3 Linear are already linear
}

vec3 encodeFromLinear(vec3 c) {
    if (colorSpaceMode == CSP_HDR10_PQ) return linear_to_pq(c);
    if (colorSpaceMode == CSP_HDR_HLG) return linear_to_hlg(c);
    if (colorSpaceMode == CSP_SDR_SRGB || colorSpaceMode == CSP_DISPLAY_P3_NONLINEAR) return linear_to_srgb(c);
    return c; // scRGB/BT2020/P3 Linear are already linear
}

// Gamut Mapping Matrices (Operate in Linear Light)
vec3 rec709_to_rec2020(vec3 c) {
    return vec3(
        0.6274 * c.r + 0.3293 * c.g + 0.0433 * c.b,
        0.0691 * c.r + 0.9195 * c.g + 0.0114 * c.b,
        0.0164 * c.r + 0.0880 * c.g + 0.8956 * c.b
    );
}

vec3 rec2020_to_rec709(vec3 c) {
    return vec3(
         1.6605 * c.r - 0.5876 * c.g - 0.0728 * c.b,
        -0.1246 * c.r + 1.1330 * c.g - 0.0084 * c.b,
        -0.0182 * c.r - 0.1006 * c.g + 1.1187 * c.b
    );
}

// Gamut Mapping Matrices (Rec.709 <-> Display P3 D65)
vec3 rec709_to_p3(vec3 c) {
    return vec3(
        0.8225 * c.r + 0.1774 * c.g + 0.0000 * c.b,
        0.0332 * c.r + 0.9669 * c.g + 0.0000 * c.b,
        0.0171 * c.r + 0.0724 * c.g + 0.9108 * c.b
    );
}

vec3 p3_to_rec709(vec3 c) {
    return vec3(
         1.2249 * c.r - 0.2247 * c.g + 0.0000 * c.b,
        -0.0420 * c.r + 1.0419 * c.g + 0.0000 * c.b,
        -0.0196 * c.r - 0.0786 * c.g + 1.0984 * c.b
    );
}

// Luma Coefficients
const vec3 LUMA_REC709  = vec3(0.2126, 0.7152, 0.0722);
const vec3 LUMA_REC2020 = vec3(0.2627, 0.6780, 0.0593);
const vec3 LUMA_P3      = vec3(0.2289, 0.6917, 0.0793); // SMPTE RP 431-2 (DCI-P3 D65 / Display P3)

#endif
