#pragma once

#include "format.hpp"

namespace vkBasalt {

    // Transfer Functions (Decode: Encoded -> Linear)
    float srgbToLinear(float c);
    float pqToLinear(float c);
    float hlgToLinear(float c);
    
    // Transfer Functions (Encode: Linear -> Encoded)
    float linearToSrgb(float c);
    float linearToPq(float c);
    float linearToHlg(float c);

    // Unified Dispatchers Decodes an encoded pixel value to linear light based on the active color space.
    float decodeColor(float c, ColorSpaceMode csm);
    
    // Encodes a linear light pixel value to the target color space.
    float encodeColor(float c, ColorSpaceMode csm);

    // Tonemappers (Linear -> [0,1])
    float tonemapReinhard(float c);
    float tonemapACES(float c);

    // Luma Coefficients
    static const float LUMA_REC709[3]  = { 0.2126f, 0.7152f, 0.0722f };
    static const float LUMA_REC2020[3] = { 0.2627f, 0.6780f, 0.0593f };

    // Gamut Mapping (Rec.709 <-> Rec.2020) Operates on linear light RGB
    void rec709ToRec2020(float& r, float& g, float& b);
    void rec2020ToRec709(float& r, float& g, float& b);

    // Gamut Mapping (Rec.709 <-> Display P3 D65) Operates on linear light RGB
    void rec709ToDisplayP3(float& r, float& g, float& b);
    void displayP3ToRec709(float& r, float& g, float& b);

} // namespace vkBasalt
