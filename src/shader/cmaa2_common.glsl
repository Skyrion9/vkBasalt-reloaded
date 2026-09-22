#ifndef CMAA2_COMMON_GLSL
#define CMAA2_COMMON_GLSL

#extension GL_GOOGLE_include_directive : enable
#include "color_space.h"

#define CMAA2_CS_INPUT_KERNEL_SIZE_X          16
#define CMAA2_CS_INPUT_KERNEL_SIZE_Y          16
#define CMAA2_CS_OUTPUT_KERNEL_SIZE_X         (CMAA2_CS_INPUT_KERNEL_SIZE_X - 2)
#define CMAA2_CS_OUTPUT_KERNEL_SIZE_Y         (CMAA2_CS_INPUT_KERNEL_SIZE_Y - 2)
#define CMAA2_PROCESS_CANDIDATES_NUM_THREADS  128
#define CMAA2_DEFERRED_APPLY_NUM_THREADS      32
#define CMAA2_DEFERRED_APPLY_THREADGROUP_SWAP 1
#define CMAA2_COLLECT_EXPAND_BLEND_ITEMS      1

#define CTRL_ITEM_COUNT       3u
#define CTRL_SHAPE_CANDIDATES 4u
#define CTRL_BLEND_LOCATIONS  8u
#define CTRL_BLEND_ITEMS      12u

// Memory access qualifiers for cache routing optimization, defined per shader explicitly to hint read/write intent.
#ifndef CMAA2_QUAL_EDGE
#define CMAA2_QUAL_EDGE
#endif
#ifndef CMAA2_QUAL_SHAPE_CAND
#define CMAA2_QUAL_SHAPE_CAND
#endif
#ifndef CMAA2_QUAL_BLEND_LOC
#define CMAA2_QUAL_BLEND_LOC
#endif
#ifndef CMAA2_QUAL_BLEND_ITEM
#define CMAA2_QUAL_BLEND_ITEM
#endif
#ifndef CMAA2_QUAL_HEADS
#define CMAA2_QUAL_HEADS
#endif
#ifndef CMAA2_QUAL_CONTROL
#define CMAA2_QUAL_CONTROL
#endif
#ifndef CMAA2_QUAL_EXECUTE
#define CMAA2_QUAL_EXECUTE
#endif
#ifndef CMAA2_QUAL_OUT
#define CMAA2_QUAL_OUT
#endif

// Descriptor bindings
layout(set = 0, binding = 0) uniform sampler2D g_inoutColorReadonly;
layout(set = 0, binding = 2, r8ui) CMAA2_QUAL_EDGE uniform uimage2D g_workingEdges;
layout(set = 0, binding = 3) CMAA2_QUAL_SHAPE_CAND buffer ShapeCandidates
{
    uint data[];
}
g_workingShapeCandidates;
layout(set = 0, binding = 4) CMAA2_QUAL_BLEND_LOC buffer BlendLocationList
{
    uint data[];
}
g_workingDeferredBlendLocationList;
layout(set = 0, binding = 5) CMAA2_QUAL_BLEND_ITEM buffer BlendItemList
{
    uvec2 data[];
}
g_workingDeferredBlendItemList;
layout(set = 0, binding = 6, r32ui) CMAA2_QUAL_HEADS uniform uimage2D g_workingDeferredBlendItemListHeads;
layout(set = 0, binding = 7) CMAA2_QUAL_CONTROL buffer ControlBuffer
{
    uint data[];
}
g_workingControlBuffer;
layout(set = 0, binding = 8) CMAA2_QUAL_EXECUTE buffer ExecuteIndirectBuf
{
    uint data[];
}
g_workingExecuteIndirectBuffer;

// Output image format variants
layout(set = 0, binding = 9, rgba8) CMAA2_QUAL_OUT uniform image2D g_outRGBA8;
layout(set = 0, binding = 10, rgb10_a2) CMAA2_QUAL_OUT uniform image2D g_outRGB10A2;
layout(set = 0, binding = 11, rgba16f) CMAA2_QUAL_OUT uniform image2D g_outRGBA16F;

// Specialization constants
layout(constant_id = 0) const float cmaa2EdgeThreshold           = 0.07;
layout(constant_id = 1) const float cmaa2LocalContrastAdaptation = 0.10;
layout(constant_id = 2) const float cmaa2SimpleShapeBluriness    = 0.10;
layout(constant_id = 3) const int cmaa2FormatVariant             = 0; // 0=RGBA8, 1=RGB10A2, 2=RGBA16F
layout(constant_id = 4) const int cmaa2DebugAA                   = 0;
layout(constant_id = 5) const int cmaa2DebugEdges                = 0;
layout(constant_id = 6) const int cmaa2ExtraSharpness            = 0;
// constant_id 65535 reserved by colorSpaceMode color_space.h which we include

// Screen resolution (baked at pipeline creation time, changes only on window resize)
layout(constant_id = 7) const uint resX = 1920;
layout(constant_id = 8) const uint resY = 1080;

// Z-shape tracing parameters (tunable via UI, baked at pipeline creation)
layout(constant_id = 9) const uint cmaa2MaxLineLength     = 86u;  // Max Z-line arm trace distance (32-128, even values)
layout(constant_id = 10) const float cmaa2MinShapeLength  = 5.0;  // Min combined arm length to qualify as Z-shape
layout(constant_id = 11) const float cmaa2ArmRatio        = 1.25; // Max ratio between longer and shorter arm
layout(constant_id = 12) const int cmaa2EdgeDetectionMode = 0;    // 0 = Luminance (Fast), 1 = Color (High Quality)

// ALU helpers
uint PackEdges(vec4 edges)
{
    return uint(dot(edges, vec4(1.0, 2.0, 4.0, 8.0)));
}

vec4 UnpackEdgesFlt(uint value)
{
    return vec4(
        (value & 0x01u) != 0u ? 1.0 : 0.0, (value & 0x02u) != 0u ? 1.0 : 0.0, (value & 0x04u) != 0u ? 1.0 : 0.0,
        (value & 0x08u) != 0u ? 1.0 : 0.0);
}

vec3 LoadSourceColor(ivec2 pixelPos, ivec2 offset)
{
    ivec2 coord = pixelPos + offset;
    coord.x     = clamp(coord.x, 0, int(resX) - 1);
    coord.y     = clamp(coord.y, 0, int(resY) - 1);
    vec3 raw    = texelFetch(g_inoutColorReadonly, coord, 0).rgb;
    return decodeToSpatial(raw);
}

uint FloatToHalf(float f)
{
    return packHalf2x16(vec2(f, 0.0)) & 0xFFFFu;
}
float HalfToFloat(uint h)
{
    return unpackHalf2x16(h).x;
}

uint Pack_R11G11B10_FLOAT(vec3 rgb)
{
    rgb    = min(rgb, vec3(uintBitsToFloat(0x477C0000u)));
    uint r = ((FloatToHalf(rgb.x) + 8u) >> 4u) & 0x000007FFu;
    uint g = ((FloatToHalf(rgb.y) + 8u) << 7u) & 0x003FF800u;
    uint b = ((FloatToHalf(rgb.z) + 16u) << 17u) & 0xFFC00000u;
    return r | g | b;
}

vec3 Unpack_R11G11B10_FLOAT(uint rgb)
{
    return vec3(
        HalfToFloat((rgb << 4u) & 0x7FF0u), HalfToFloat((rgb >> 7u) & 0x7FF0u), HalfToFloat((rgb >> 17u) & 0x7FE0u));
}

uint Pack_R11G11B10_E4_FLOAT(vec3 rgb)
{
    rgb    = clamp(rgb, vec3(0.0), vec3(uintBitsToFloat(0x3FFFFFFFu)));
    uint r = ((FloatToHalf(rgb.x) + 4u) >> 3u) & 0x000007FFu;
    uint g = ((FloatToHalf(rgb.y) + 4u) << 8u) & 0x003FF800u;
    uint b = ((FloatToHalf(rgb.z) + 8u) << 18u) & 0xFFC00000u;
    return r | g | b;
}

vec3 Unpack_R11G11B10_E4_FLOAT(uint rgb)
{
    return vec3(
        HalfToFloat((rgb << 3u) & 0x3FF8u), HalfToFloat((rgb >> 8u) & 0x3FF8u), HalfToFloat((rgb >> 18u) & 0x3FF0u));
}

uint InternalPackColor(vec3 color)
{
    if (isHDR)
        return Pack_R11G11B10_FLOAT(color);
    else
        return Pack_R11G11B10_E4_FLOAT(color);
}

vec3 InternalUnpackColor(uint packedColor)
{
    if (isHDR)
        return Unpack_R11G11B10_FLOAT(packedColor);
    else
        return Unpack_R11G11B10_E4_FLOAT(packedColor);
}

// Resource touching helpers: gated by shader role to avoid glslang validating writes to readonly resources (or reads from writeonly) in shaders
// that never call these functions. glslang validates all function bodies regardless of whether main() calls them.

// StoreColorSample writes to: ControlBuffer (atomicAdd), BlendItemList, BlendItemListHeads (imageAtomicExchange), BlendLocationList.
#ifdef CMAA2_HELPER_STORE_COLOR_SAMPLE
void StoreColorSample(ivec2 pixelPos, vec3 color, bool isComplexShape)
{
    uvec2 headsDims = (uvec2(resX, resY) + uvec2(1, 1)) / uvec2(2, 2);
    ivec2 quadPos   = pixelPos / 2;

    // Prevent OOB imageAtomicExchange GPU page faults
    if (quadPos.x < 0 || quadPos.y < 0 || uint(quadPos.x) >= headsDims.x || uint(quadPos.y) >= headsDims.y) return;
    uint counterIndex = atomicAdd(g_workingControlBuffer.data[CTRL_BLEND_ITEMS], 1u);

    // Vulkan has no hardware bounds checking for SSBOs. If we exceed the buffer size, we would corrupt adjacent heap memory.
    if (counterIndex >= g_workingDeferredBlendItemList.data.length()) return;
    uint offsetXY               = (uint(pixelPos.y) % 2u) * 2u + (uint(pixelPos.x) % 2u);
    uint header                 = (offsetXY << 30u) | (uint(isComplexShape) << 26u);
    uint counterIndexWithHeader = counterIndex | header;

    uint originalIndex =
        imageAtomicExchange(g_workingDeferredBlendItemListHeads, ivec2(quadPos), counterIndexWithHeader);
    g_workingDeferredBlendItemList.data[counterIndex] = uvec2(originalIndex, InternalPackColor(color));

    if (originalIndex == 0xFFFFFFFFu) {
        uint edgeListCounter = atomicAdd(g_workingControlBuffer.data[CTRL_BLEND_LOCATIONS], 1u);
        if (edgeListCounter < g_workingDeferredBlendLocationList.data.length()) {
            g_workingDeferredBlendLocationList.data[edgeListCounter] = (quadPos.x << 16u) | quadPos.y;
        }
    }
}
#endif // CMAA2_HELPER_STORE_COLOR_SAMPLE

// LoadEdge reads from g_workingEdges via imageLoad.
#ifdef CMAA2_HELPER_LOAD_EDGE
uint LoadEdge(ivec2 pixelPos, ivec2 offset)
{
    ivec2 coord       = pixelPos + offset;
    uint logicalWidth = resX;
    uint height       = resY;
    // OOB reads return 0 Z-tracing terminates cleanly at screen borders.
    if (coord.x < 0 || coord.y < 0 || uint(coord.x) >= logicalWidth || uint(coord.y) >= height) return 0u;
    // Determine if we are the left (0) or right (1) pixel of the packed pair
    uint a = uint(coord.x) % 2u;
    // Read the 8 bit packed texel
    uint packed = imageLoad(g_workingEdges, ivec2(coord.x / 2, coord.y)).x;
    // Shift and mask to extract the 4 bit edge value
    return (packed >> (a * 4u)) & 0x0Fu;
}
#endif // CMAA2_HELPER_LOAD_EDGE

#endif // CMAA2_COMMON_GLSL
