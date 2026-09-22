///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2018, Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Original Author(s): Filip Strugar, Adam Lake (Intel Corporation)
// Original Source:    https://github.com/GameTechDev/CMAA2
//
// MODIFICATIONS BY Skyrion9/vkBasalt-reloaded:
// - Ported from DirectX 12 HLSL to Vulkan GLSL compute shaders.
// - Replaced per thread global atomics with GL_KHR_shader_subgroup_ballot reduction in canditates to reduce contention.
// - Added inplace slice optimization for the vkBasalt effect chain architecture.
// - Replaced compile time HLSL macros with Vulkan specialization constants with more configuration knobs.
// - Integrated with vkBasalt-reloaded's color_space.h for HDR (PQ/HLG) and SDR edge detection.
// - Removed MSAA support (vkBasalt-reloaded operates on the final resolved swapchain image).
// - Added cache routing qualifiers and conditional helper compilation for glslang.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 450
#extension GL_GOOGLE_include_directive : enable

#define CMAA2_SHADER_APPLY 1
// Cache routing: this shader reads blend lists/heads/control and writes to output images.
#define CMAA2_QUAL_BLEND_LOC  readonly
#define CMAA2_QUAL_BLEND_ITEM readonly
#define CMAA2_QUAL_HEADS      readonly
#define CMAA2_QUAL_CONTROL    readonly
#define CMAA2_QUAL_OUT        writeonly

#include "cmaa2_common.glsl"

#if CMAA2_DEFERRED_APPLY_THREADGROUP_SWAP
layout(local_size_x = 4, local_size_y = CMAA2_DEFERRED_APPLY_NUM_THREADS, local_size_z = 1) in;
#else
layout(local_size_x = CMAA2_DEFERRED_APPLY_NUM_THREADS, local_size_y = 4, local_size_z = 1) in;
#endif

void main()
{
    uint numCandidates = g_workingControlBuffer.data[CTRL_ITEM_COUNT];
#if CMAA2_DEFERRED_APPLY_THREADGROUP_SWAP
    uint currentCandidate    = gl_GlobalInvocationID.y;
    uint currentQuadOffsetXY = gl_LocalInvocationID.x;
#else
    uint currentCandidate    = gl_GlobalInvocationID.x;
    uint currentQuadOffsetXY = gl_LocalInvocationID.y;
#endif
    if (currentCandidate >= numCandidates) return;

    uint pixelID             = g_workingDeferredBlendLocationList.data[currentCandidate];
    uvec2 quadPos            = uvec2(pixelID >> 16u, pixelID & 0xFFFFu);
    const ivec2 qeOffsets[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
    ivec2 pixelPos           = ivec2(quadPos) * 2 + qeOffsets[currentQuadOffsetXY];

    // Bounds check to prevent OOB imageStore crashes on the swapchain image. While StoreColorSample guarantees quadPos
    // is within the heads image, odd resolutions or padding can cause pixelPos to exceed the exact swapchain dimensions.
    uvec2 outDims = uvec2(resX, resY);
    if (uint(pixelPos.x) >= outDims.x || uint(pixelPos.y) >= outDims.y) return;

    uint counterIndexWithHeader = imageLoad(g_workingDeferredBlendItemListHeads, ivec2(quadPos)).x;
    vec4 outColors              = vec4(0.0);
    const uint maxLoops         = 32u;

    for (uint i = 0u; (counterIndexWithHeader != 0xFFFFFFFFu) && (i < maxLoops); i++) {
        uint offsetXY          = (counterIndexWithHeader >> 30u) & 0x03u;
        bool isComplexShape    = ((counterIndexWithHeader >> 26u) & 0x01u) != 0u;
        uvec2 val              = g_workingDeferredBlendItemList.data[counterIndexWithHeader & ((1u << 26u) - 1u)];
        counterIndexWithHeader = val.x;
        if (offsetXY == currentQuadOffsetXY) {
            vec3 color   = InternalUnpackColor(val.y);
            float weight = 0.8 + 1.0 * float(isComplexShape);
            outColors += vec4(color * weight, weight);
        }
    }
    if (outColors.a == 0.0) return;

    vec3 outColor = outColors.rgb / outColors.a;

    if (cmaa2DebugAA == 1) {
        float intensity = clamp(outColors.a, 0.0, 1.0);
        outColor        = mix(vec3(0.0), vec3(0.0, 1.0, 1.0), intensity);
    }

    vec4 finalColor = vec4(encodeFromSpatial(outColor), 1.0);

    if (cmaa2FormatVariant == 0) {
        imageStore(g_outRGBA8, pixelPos, finalColor);
    } else if (cmaa2FormatVariant == 1) {
        imageStore(g_outRGB10A2, pixelPos, finalColor);
    } else {
        imageStore(g_outRGBA16F, pixelPos, finalColor);
    }
}
