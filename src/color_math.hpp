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

} // namespace vkBasalt
