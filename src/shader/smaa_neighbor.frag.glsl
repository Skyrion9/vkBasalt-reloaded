#version 450
#extension GL_GOOGLE_include_directive : enable
#include "smaa_settings.h"
#include "color_space.h"

layout(set = 0, binding = 0) uniform sampler2D colorImg;
layout(set = 0, binding = 4) uniform sampler2D blendTex;

layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec2 textureCoord;
layout(location = 1) in vec4 offset;

const bool isHDR = (colorSpaceMode != CSP_SDR_SRGB);

vec4 SMAADecodeFetch(vec4 raw) {
    return vec4(decodeToLinear(raw.rgb), raw.a);
}
#define SMAASamplePointColor(tex, coord) SMAADecodeFetch(textureLod(tex, coord, 0.0))
#define SMAASampleLevelZeroColor(tex, coord) SMAADecodeFetch(textureLod(tex, coord, 0.0))

#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa.h"

void main()
{
    vec4 smaaColor = SMAANeighborhoodBlendingPS(textureCoord, offset, colorImg, blendTex);
    
    // Encode back to target color space, then clamp appropriately
    vec3 encodedColor = encodeFromLinear(smaaColor.rgb);
    vec3 outColor = isHDR ? max(encodedColor, 0.0) : clamp(encodedColor, 0.0, 1.0);
    
    fragColor = vec4(outColor, smaaColor.a);
}
