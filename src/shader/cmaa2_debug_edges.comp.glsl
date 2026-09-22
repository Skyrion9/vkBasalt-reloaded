#version 450
#extension GL_GOOGLE_include_directive : enable

#define CMAA2_SHADER_DEBUG 1
// Cache routing: reads edges, writes to output images.
#define CMAA2_QUAL_EDGE readonly
#define CMAA2_QUAL_OUT  writeonly

// Enable helper needed by this shader
#define CMAA2_HELPER_LOAD_EDGE

#include "cmaa2_common.glsl"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// 5 stage thermal ramp: (cold) dark -> purple -> red -> orange -> white (hot)
// Maps normalized edge intensity [0, 1] to a perceptually ordered heat color.
vec3 thermalHeatmap(float t)
{
    vec3 c0 = vec3(0.02, 0.02, 0.05); // 0.00: Near-black (no edges)
    vec3 c1 = vec3(0.20, 0.00, 0.40); // 0.25: Deep purple (1 edge)
    vec3 c2 = vec3(0.80, 0.10, 0.10); // 0.50: Red (2 edges)
    vec3 c3 = vec3(1.00, 0.60, 0.00); // 0.75: Orange (3 edges)
    vec3 c4 = vec3(1.00, 1.00, 0.80); // 1.00: White-hot (4 edges)

    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}

void main()
{
    ivec2 pixelPos = ivec2(gl_GlobalInvocationID.xy);

    // Bounds check: the 16x16 dispatch rounds up, so edge threads can exceed swapchain dimensions.
    uvec2 outDims = uvec2(resX, resY);
    if (uint(pixelPos.x) >= outDims.x || uint(pixelPos.y) >= outDims.y) return;

    // Use LoadEdge() to correctly handle the half width R8_UINT packed texture.
    uint edgesPacked = LoadEdge(pixelPos, ivec2(0));
    vec4 edges       = UnpackEdgesFlt(edgesPacked);

    // Layer 1: Thermal heatmap (intensity) - edges.x = Right, edges.y = Bottom, edges.z = Left, edges.w = Top
    float edgeCount = dot(edges, vec4(1.0));
    float intensity = edgeCount * 0.25; // normalize to [0, 1]
    vec3 heatColor  = thermalHeatmap(intensity);

    // Layer 2: Directional tint (orientation; Horizontal Axis: Cool tones (Cyan/Blue), Vertical Axis: Bright/Warm tones (Green/Yellow)
    vec3 dirColor = vec3(0.0);
    dirColor += vec3(0.000, 0.898, 1.000) * edges.x; // Right  (Electric Cyan)
    dirColor += vec3(0.000, 1.000, 0.400) * edges.y; // Bottom (Spring Green)
    dirColor += vec3(0.102, 0.337, 1.000) * edges.z; // Left   (Cobalt Blue)
    dirColor += vec3(1.000, 0.902, 0.000) * edges.w; // Top    (Electric Yellow)

    float dirStrength = 0.35 * (1.0 - intensity * 0.4);
    vec3 finalRGB     = heatColor + dirColor * dirStrength;

    vec4 finalColor = vec4(encodeFromSpatial(finalRGB), 1.0);

    if (cmaa2FormatVariant == 0) {
        imageStore(g_outRGBA8, pixelPos, finalColor);
    } else if (cmaa2FormatVariant == 1) {
        imageStore(g_outRGB10A2, pixelPos, finalColor);
    } else {
        imageStore(g_outRGBA16F, pixelPos, finalColor);
    }
}
