#ifndef COLOR_SPACE_GLSL
#define COLOR_SPACE_GLSL

#define CSP_SDR_SRGB  0
#define CSP_HDR10_PQ  1
#define CSP_HDR_HLG   2
#define CSP_HDR_SCRGB 3

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

// PQ linear: 1.0 = 10,000 nits. Scale by 100 so 1.0 = SDR white (100 nits),
// matching the scRGB convention that hdrNorm thresholds are calibrated for.
vec3 pq_to_linear(vec3 c) {
    vec3 N = pow(max(c, vec3(0.0)), vec3(1.0 / PQ_M2));
    vec3 linear = pow(max(N - PQ_C1, vec3(0.0)) / (PQ_C2 - PQ_C3 * N), vec3(1.0 / PQ_M1));
    return linear * 100.0;
}
vec3 linear_to_pq(vec3 c) {
    vec3 L = pow(max(c / 100.0, vec3(0.0)), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * L) / (vec3(1.0) + PQ_C3 * L), vec3(PQ_M2));
}

//  HLG (Hybrid Log-Gamma) Transfer Functions 
const float HLG_A = 0.17883277;
const float HLG_B = 0.28466892;
const float HLG_C = 0.55991073;

// HLG linear: 1.0 ≈ 1000 nits on a reference display. Scale by 10 so
// SDR white (~100 nits, scene linear ~0.1) maps to ~1.0.
vec3 hlg_to_linear(vec3 c) {
    vec3 linear = mix(c * c / 3.0, (exp((c - HLG_C) / HLG_A) + HLG_B) / 12.0, step(vec3(0.5), c));
    return linear * 10.0;
}
vec3 linear_to_hlg(vec3 c) {
    vec3 L = max(c / 10.0, vec3(0.0));
    return mix(sqrt(3.0 * L), HLG_A * log(12.0 * L - HLG_B) + HLG_C, step(vec3(1.0 / 12.0), L));
}

//  Unified Decode/Encode Wrappers 
vec3 decodeToLinear(vec3 c) {
    if (colorSpaceMode == CSP_HDR10_PQ) return pq_to_linear(c);
    if (colorSpaceMode == CSP_HDR_HLG) return hlg_to_linear(c);
    return c; // scRGB is already linear; SDR works in gamma space
}

vec3 encodeFromLinear(vec3 c) {
    if (colorSpaceMode == CSP_HDR10_PQ) return linear_to_pq(c);
    if (colorSpaceMode == CSP_HDR_HLG) return linear_to_hlg(c);
    return c; // scRGB is already linear; SDR works in gamma space
}

#endif
