#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

layout(set=0, binding=0) uniform sampler2D img;
layout(set=1, binding=0) uniform sampler3D lut;

//Only works with cubes not with cuboids
layout(constant_id = 0) const int lutSize = 32;
layout(constant_id = 1) const int flipGB = 0;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

const bool isHDR = (colorSpaceMode != CSP_SDR_SRGB);

#define textureLod0Offset(img, coord, offset) textureLodOffset(img, coord, 0.0f, offset)
#define textureLod0(img, coord) textureLod(img, coord, 0.0f)

void main()
{
    vec4 rawColor;
    if(flipGB != 0)
    {
        rawColor = textureLod0(img,textureCoord).rbga;
    }
    else
    {
        rawColor = textureLod0(img,textureCoord);
    }

    // Decode swapchain to linear light
    vec3 linearColor = decodeToLinear(rawColor.rgb);

    // LUTs are typically authored in sRGB/gamma space. 
    // To correctly apply the LUT in an HDR pipeline, we isolate the SDR white range [0, 1] in linear space,
    // convert it to sRGB gamma for the LUT sampling, and preserve the HDR excess to add back later.
    vec3 excess = max(linearColor - 1.0, 0.0);
    vec3 clampedLinear = min(linearColor, 1.0);
    
    // Convert to sRGB for the LUT, apply LUT, then convert back to linear
    vec3 lutInput = linear_to_srgb(clampedLinear);
    
    //see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
    vec3 scale = (vec3(lutSize) - 1.0) / vec3(lutSize);
    vec3 offset = 1.0 / (2.0 * vec3(lutSize));
    
    vec3 lutColor = textureLod0(lut, scale * lutInput + offset).rgb;
    vec3 lutColorLinear = srgb_to_linear(lutColor);

    // Add back HDR excess to preserve highlight intensity
    vec3 finalLinear = lutColorLinear + excess;
    
    // Encode back to target color space
    vec3 finalEncoded = encodeFromLinear(finalLinear);
    
    // Clamp appropriately
    vec3 outColor = isHDR ? max(finalEncoded, 0.0) : clamp(finalEncoded, 0.0, 1.0);
    fragColor = vec4(outColor, rawColor.a);
}
