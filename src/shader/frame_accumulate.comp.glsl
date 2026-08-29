#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

// Frame Analyzer Accumulate Pass decodes the input image and bins pixels into three SSBOs.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) buffer HistBuffer  { uint histData[1024]; };
layout(set = 0, binding = 2) buffer WaveBuffer  { uint waveData[65536]; };
layout(set = 0, binding = 3) buffer VecBuffer   { uint vecData[65536]; };

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
    uint enabled;
} pc;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);
    vec4 raw = textureLod(inputImage, uv, 0.0);
    
    vec3 linear = decodeToLinear(raw.rgb);

    // Luma (Rec. 709)
    float luma = dot(linear, vec3(0.2126, 0.7152, 0.0722));

    // Clamp for binning (HDR highlights accumulate at bin 255)
    vec3 clamped = clamp(linear, 0.0, 1.0);
    float lumaClamped = clamp(luma, 0.0, 1.0);

    uint binR = uint(clamped.r * 255.0);
    uint binG = uint(clamped.g * 255.0);
    uint binB = uint(clamped.b * 255.0);
    uint binL = uint(lumaClamped * 255.0);

    atomicAdd(histData[binR], 1u);
    atomicAdd(histData[256u + binG], 1u);
    atomicAdd(histData[512u + binB], 1u);
    atomicAdd(histData[768u + binL], 1u);

    // Waveform: X position vs luminance
    uint waveX = uint((float(pixel.x) / float(pc.width)) * 255.0);
    uint waveY = uint(lumaClamped * 255.0);
    atomicAdd(waveData[waveY * 256u + waveX], 1u);

    // Vectorscope: Cb vs Cr
    float cb = 0.5 + (linear.b - luma) / 1.8556;
    float cr = 0.5 + (linear.r - luma) / 1.5748;
    cb = clamp(cb, 0.0, 1.0);
    cr = clamp(cr, 0.0, 1.0);
    uint vecX = uint(cb * 255.0);
    uint vecY = uint((1.0 - cr) * 255.0);
    atomicAdd(vecData[vecY * 256u + vecX], 1u);
}
