#version 450
#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

// Frame Analyzer: Resolve Pass reads accumulated SSBO data, normalizes, applies heat map, and writes to 256x256 output images.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) buffer HistBuffer  { uint histData[1024]; };
layout(set = 0, binding = 1) buffer WaveBuffer  { uint waveData[65536]; };
layout(set = 0, binding = 2) buffer VecBuffer   { uint vecData[65536]; };

layout(set = 0, binding = 3, rgba8) uniform writeonly image2D histImage;
layout(set = 0, binding = 4, rgba8) uniform writeonly image2D waveImage;
layout(set = 0, binding = 5, rgba8) uniform writeonly image2D vecImage;

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
    uint enabled;
} pc;

vec3 heatMap(float v) {
    v = clamp(v, 0.0, 1.0);
    if (v < 0.25) return mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), v * 4.0);
    if (v < 0.5)  return mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), (v - 0.25) * 4.0);
    if (v < 0.75) return mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), (v - 0.5) * 4.0);
    return vec3(1.0, 1.0, 1.0);
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= 256 || coord.y >= 256) return;

    if (pc.enabled == 0u) {
        imageStore(histImage, coord, vec4(0.0));
        imageStore(waveImage, coord, vec4(0.0));
        imageStore(vecImage,  coord, vec4(0.0));
        return;
    }

    // Histogram: vertical bars, X = bin, Y = count (0 at bottom)
    uint bin = uint(coord.x);
    uint intensity = 255u - uint(coord.y);

    float maxVal = float(pc.width * pc.height) / 256.0 * 2.0;
    float rVal = float(histData[bin]) / maxVal;
    float gVal = float(histData[256u + bin]) / maxVal;
    float bVal = float(histData[512u + bin]) / maxVal;
    float lVal = float(histData[768u + bin]) / maxVal;

    vec4 histColor = vec4(0.05, 0.05, 0.08, 1.0); // dark background
    float normI = float(intensity) / 255.0;
    if (normI < rVal) histColor.r = 1.0;
    if (normI < gVal) histColor.g = 1.0;
    if (normI < bVal) histColor.b = 1.0;
    if (normI < lVal) histColor = vec4(1.0, 1.0, 1.0, 1.0); // luma = white overlay
    imageStore(histImage, coord, histColor);

    // Waveform: heat mapped density
    uint waveIdx = uint(coord.y) * 256u + uint(coord.x);
    float waveVal = float(waveData[waveIdx]) / 50.0;
    imageStore(waveImage, coord, vec4(heatMap(waveVal), 1.0));

    // Vectorscope: heat mapped density
    uint vecIdx = uint(coord.y) * 256u + uint(coord.x);
    float vecVal = float(vecData[vecIdx]) / 30.0;
    imageStore(vecImage, coord, vec4(heatMap(vecVal), 1.0));
}
