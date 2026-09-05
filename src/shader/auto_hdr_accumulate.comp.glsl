#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) buffer HistogramBuffer { uint bins[256]; };

// Resolution and color space are constant per pipeline lifetime -> Spec Constants
layout(constant_id = 0) const uint width = 1920;
layout(constant_id = 1) const uint height = 1080;
layout(constant_id = 2) const float invWidth = 0.000520833;
layout(constant_id = 3) const float invHeight = 0.000925925;

shared uint sharedBins[256];

void main() {
    // Parallel initialization: 16x16 = 256 threads for the 256 bins
    sharedBins[gl_LocalInvocationIndex] = 0;
    barrier();

    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x < int(width) && pixel.y < int(height)) {
        vec2 uv = (vec2(pixel) + 0.5) * vec2(invWidth, invHeight);
        vec4 raw = textureLod(inputImage, uv, 0.0);
        vec3 linear = decodeToLinear(raw.rgb);
        
        // Use correct luma coefficients based on source color space scRGB and SDR use Rec.709 primaries. P3 uses P3 primaries. PQ/HLG/BT.2020 use Rec.2020.
        vec3 lumaCoeffs = LUMA_REC709;
        if (colorSpaceMode == CSP_HDR10_PQ || 
            colorSpaceMode == CSP_HDR_HLG || 
            colorSpaceMode == CSP_HDR_BT2020_LINEAR) {
            lumaCoeffs = LUMA_REC2020;
        } else if (colorSpaceMode == CSP_HDR_DISPLAY_P3_LINEAR || 
                colorSpaceMode == CSP_DISPLAY_P3_NONLINEAR) {
            lumaCoeffs = LUMA_P3;
        }
            
        float luma = dot(linear, lumaCoeffs);
        uint bin = uint(clamp(luma, 0.0, 1.0) * 255.0);
        
        atomicAdd(sharedBins[bin], 1);
    }
    
    barrier();
    
    // Parallel flush to global memory
    if (sharedBins[gl_LocalInvocationIndex] > 0) {
        atomicAdd(bins[gl_LocalInvocationIndex], sharedBins[gl_LocalInvocationIndex]);
    }
}
