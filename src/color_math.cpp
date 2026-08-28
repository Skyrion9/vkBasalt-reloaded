#include "color_math.hpp"
#include <cmath>
#include <algorithm>

namespace vkBasalt {

    //  sRGB 
    float srgbToLinear(float c) {
        c = std::max(c, 0.0f);
        return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
    }

    float linearToSrgb(float c) {
        c = std::max(c, 0.0f);
        return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
    }

    // PQ (ST 2084 / HDR10) 1.0 in linear space = 100 nits (SDR white reference). The absolute peak of PQ is 10,000 nits (linear value 100.0).
    static const float PQ_M1 = 0.1593017578125f;
    static const float PQ_M2 = 78.84375f;
    static const float PQ_C1 = 0.8359375f;
    static const float PQ_C2 = 18.8515625f;
    static const float PQ_C3 = 18.6875f;

    float pqToLinear(float c) {
        c = std::max(c, 0.0f);
        float Np = powf(c, 1.0f / PQ_M2);
        float num = std::max(Np - PQ_C1, 0.0f);
        float den = std::max(PQ_C2 - PQ_C3 * Np, 1e-5f);
        float L = powf(num / den, 1.0f / PQ_M1);
        return L * 100.0f; // Scale to 1.0 = SDR white (100 nits)
    }

    float linearToPq(float c) {
        float L = std::max(c / 100.0f, 0.0f); // Normalize from 1.0 = 100 nits
        float Lm1 = powf(L, PQ_M1);
        float num = PQ_C1 + PQ_C2 * Lm1;
        float den = 1.0f + PQ_C3 * Lm1;
        return powf(num / den, PQ_M2);
    }

    // HLG (Hybrid Log Gamma) 1.0 in linear space ≈ 100 nits (SDR white reference). HLG nominal peak is usually 1000 nits (linear value 10.0).
    static const float HLG_A = 0.17883277f;
    static const float HLG_B = 0.28466892f;
    static const float HLG_C = 0.55991073f;

    float hlgToLinear(float c) {
        c = std::max(c, 0.0f);
        float L = (c <= 0.5f) ? (c * c / 3.0f) : ((expf((c - HLG_C) / HLG_A) + HLG_B) / 12.0f);
        return L * 10.0f; // Scale to 1.0 = SDR white
    }

    float linearToHlg(float c) {
        float L = std::max(c / 10.0f, 0.0f); // Normalize from 1.0 = 100 nits
        return (L <= 1.0f / 12.0f) ? sqrtf(3.0f * L) : (HLG_A * logf(12.0f * L - HLG_B) + HLG_C);
    }

    // Unified Dispatchers
    float decodeColor(float c, ColorSpaceMode csm) {
        switch (csm) {
            case ColorSpaceMode::SDR_SRGB:  return c; // SDR works in gamma space
            case ColorSpaceMode::HDR10_PQ:  return pqToLinear(c);
            case ColorSpaceMode::HDR_HLG:   return hlgToLinear(c);
            case ColorSpaceMode::HDR_SCRGB: return c; // Already linear
            default: return c;
        }
    }

    float encodeColor(float c, ColorSpaceMode csm) {
        switch (csm) {
            case ColorSpaceMode::SDR_SRGB:  return c; // SDR works in gamma space
            case ColorSpaceMode::HDR10_PQ:  return linearToPq(c);
            case ColorSpaceMode::HDR_HLG:   return linearToHlg(c);
            case ColorSpaceMode::HDR_SCRGB: return c; // Already linear
            default: return c;
        }
    }

    // Tonemappers
    float tonemapReinhard(float c) {
        return std::max(c, 0.0f) / (1.0f + std::max(c, 0.0f));
    }

    // Attempt at a simple ACES filmic curve (Narkowicz 2015 fit)
    float tonemapACES(float c) {
        c = std::max(c, 0.0f);
        float a = c * (c * 2.51f + 0.03f);
        float b = c * (c * 2.43f + 0.59f) + 0.14f;
        return std::clamp(a / b, 0.0f, 1.0f);
    }

} // namespace vkBasalt
