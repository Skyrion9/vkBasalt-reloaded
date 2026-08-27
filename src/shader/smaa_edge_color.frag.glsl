#version 450
#extension GL_GOOGLE_include_directive : enable
#include "smaa_settings.h"
#include "color_space.h"

layout(set = 0, binding = 0) uniform sampler2D colorImg;

layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec2 textureCoord;
layout(location = 1) in vec4[3] offsets;

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
    fragColor = vec4(SMAAColorEdgeDetectionPS(textureCoord, offsets, colorImg), 0.0, 0.0);
}
