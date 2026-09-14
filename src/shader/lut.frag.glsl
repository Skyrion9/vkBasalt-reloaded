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

    // Decode to spatial domain (gamma for SDR, linear nits for HDR)
    vec3 spatialColor = decodeToSpatial(rawColor.rgb);

    // LUTs are typically authored in sRGB/gamma space.  For HDR, we isolate the SDR white range [0, 1] in linear space.
    // Convert it to sRGB gamma for the LUT sampling, and preserve the HDR excess to add back later.
    vec3 excess = isHDR ? max(spatialColor - 1.0, 0.0) : vec3(0.0);
    vec3 clampedSpatial = min(spatialColor, 1.0);
    
    vec3 lutInput = isHDR ? linear_to_srgb(clampedSpatial) : clampedSpatial;
    
    //see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
    vec3 scale = (vec3(lutSize) - 1.0) / vec3(lutSize);
    vec3 offset = 1.0 / (2.0 * vec3(lutSize));
    
    vec3 lutColor = textureLod0(lut, scale * lutInput + offset).rgb;
    vec3 lutColorSpatial = isHDR ? srgb_to_linear(lutColor) : lutColor;

    // Add back HDR excess to preserve highlight intensity
    vec3 finalSpatial = lutColorSpatial + excess;
    
    // Encode back to target color space
    vec3 finalEncoded = encodeFromSpatial(finalSpatial);
    
    // Lower bound clamped, upper bound left to hardware/AutoHDR.
    vec3 outColor = max(finalEncoded, 0.0);
    fragColor = vec4(outColor, rawColor.a);
}
