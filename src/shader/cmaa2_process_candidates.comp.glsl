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

#define CMAA2_SHADER_PROCESS 1
// Cache routing: reads edges and candidates. Control remains read-write (atomics in StoreColorSample).
#define CMAA2_QUAL_EDGE       readonly
#define CMAA2_QUAL_SHAPE_CAND readonly

// Enable helpers needed by this shader
#define CMAA2_HELPER_LOAD_EDGE
#define CMAA2_HELPER_STORE_COLOR_SAMPLE

#include "cmaa2_common.glsl"

layout(local_size_x = CMAA2_PROCESS_CANDIDATES_NUM_THREADS, local_size_y = 1, local_size_z = 1) in;

#if CMAA2_COLLECT_EXPAND_BLEND_ITEMS
#define CMAA2_BLEND_ITEM_SLM_SIZE 768
shared uint g_groupSharedBlendItemCount;
shared uvec2 g_groupSharedBlendItems[CMAA2_BLEND_ITEM_SLM_SIZE];
#endif

// Symmetry correction and dampening
const float c_symmetryCorrectionOffset = 0.22;
const float c_dampeningEffect          = (cmaa2ExtraSharpness != 0) ? 0.11 : 0.15;

void FindZLineLengths(
    out float lineLengthLeft,
    out float lineLengthRight,
    ivec2 screenPos,
    bool horizontal,
    bool invertedZShape,
    vec2 stepRight)
{
    uint maskLeft, bitsContinueLeft, maskRight, bitsContinueRight;
    {
        uint maskTraceLeft, maskTraceRight;
        if (horizontal) {
            maskTraceLeft  = 0x08u; // tracing top edge
            maskTraceRight = 0x02u; // tracing bottom edge
        } else {
            maskTraceLeft  = 0x04u; // tracing left edge
            maskTraceRight = 0x01u; // tracing right edge
        }
        if (invertedZShape) {
            uint temp      = maskTraceLeft;
            maskTraceLeft  = maskTraceRight;
            maskTraceRight = temp;
        }
        maskLeft          = maskTraceLeft;
        bitsContinueLeft  = maskTraceLeft;
        maskRight         = maskTraceRight;
        bitsContinueRight = maskTraceRight;
    }

    bool continueLeft  = true;
    bool continueRight = true;
    lineLengthLeft     = 1.0;
    lineLengthRight    = 1.0;

    for (;;) {
        uint edgeLeft  = LoadEdge(screenPos - ivec2(stepRight * float(lineLengthLeft)), ivec2(0));
        uint edgeRight = LoadEdge(screenPos + ivec2(stepRight * (float(lineLengthRight) + 1.0)), ivec2(0));

        continueLeft  = continueLeft && ((edgeLeft & maskLeft) == bitsContinueLeft);
        continueRight = continueRight && ((edgeRight & maskRight) == bitsContinueRight);

        lineLengthLeft += continueLeft ? 1.0 : 0.0;
        lineLengthRight += continueRight ? 1.0 : 0.0;

        float maxLR = max(lineLengthRight, lineLengthLeft);
        if (!continueLeft && !continueRight) maxLR = float(cmaa2MaxLineLength);

        // Extra sharpness caps the arm ratio at 1.20 to preserve more sharpness
        float armRatio  = (cmaa2ExtraSharpness != 0) ? min(cmaa2ArmRatio, 1.20) : cmaa2ArmRatio;
        float armOffset = armRatio - 1.0;
        if (maxLR >= min(float(cmaa2MaxLineLength), armRatio * min(lineLengthRight, lineLengthLeft) - armOffset)) break;
    }
}

void DetectZsHorizontal(
    vec4 edges, vec4 edgesM1P0, vec4 edgesP1P0, vec4 edgesP2P0, out float invertedZScore, out float normalZScore)
{
    // Inverted Z: edges.r * edges.g * edgesP1P0.a
    invertedZScore = edges.r * edges.g * edgesP1P0.a;
    invertedZScore *= 2.0 + (edgesM1P0.g + edgesP2P0.a) - (edges.a + edgesP1P0.g)
                      - 0.7 * (edgesP2P0.g + edgesM1P0.a + edges.b + edgesP1P0.r);

    // Normal Z: edges.r * edges.a * edgesP1P0.g
    normalZScore = edges.r * edges.a * edgesP1P0.g;
    normalZScore *= 2.0 + (edgesM1P0.a + edgesP2P0.g) - (edges.g + edgesP1P0.a)
                    - 0.7 * (edgesP2P0.a + edgesM1P0.g + edges.b + edgesP1P0.r);
}

vec4 ComputeSimpleShapeBlendValues(vec4 edges, vec4 edgesLeft, vec4 edgesRight, vec4 edgesTop, vec4 edgesBottom)
{
    float fromRight = edges.r;
    float fromBelow = edges.g;
    float fromLeft  = edges.b;
    float fromAbove = edges.a;

    float blurCoeff              = cmaa2SimpleShapeBluriness;
    float numberOfEdges          = dot(edges, vec4(1.0));
    float numberOfEdgesAllAround = dot(edgesLeft.bga + edgesRight.rga + edgesTop.rba + edgesBottom.rgb, vec3(1.0));

    // L-like step shape Intel HLSL reference passes dontTestShapeValidity=true here, which skips the numberOfEdges==1 and corner mask checks.
    if (numberOfEdges == 2.0) {
        blurCoeff *= 0.75;
        float k = 0.9;
        fromRight += k * (edges.g * edgesTop.r * (1.0 - edgesLeft.g) + edges.a * edgesBottom.r * (1.0 - edgesLeft.a));
        fromBelow += k * (edges.b * edgesRight.g * (1.0 - edgesTop.b) + edges.r * edgesLeft.g * (1.0 - edgesTop.r));
        fromLeft += k * (edges.a * edgesBottom.b * (1.0 - edgesRight.a) + edges.g * edgesTop.b * (1.0 - edgesRight.g));
        fromAbove +=
            k * (edges.r * edgesLeft.a * (1.0 - edgesBottom.r) + edges.b * edgesRight.a * (1.0 - edgesBottom.b));
    }

    // Dampen blurring when lots of neighbouring edges
    if (cmaa2ExtraSharpness != 0)
        blurCoeff *= clamp(1.15 - numberOfEdgesAllAround / 8.0, 0.0, 1.0);
    else
        blurCoeff *= clamp(1.30 - numberOfEdgesAllAround / 10.0, 0.0, 1.0);

    return vec4(fromLeft, fromAbove, fromRight, fromBelow) * blurCoeff;
}

#if CMAA2_COLLECT_EXPAND_BLEND_ITEMS
bool CollectBlendZs(
    ivec2 screenPos,
    bool horizontal,
    bool invertedZShape,
    float shapeQualityScore,
    float lineLengthLeft,
    float lineLengthRight,
    vec2 stepRight)
{
    float leftOdd  = c_symmetryCorrectionOffset * float(uint(lineLengthLeft) % 2u);
    float rightOdd = c_symmetryCorrectionOffset * float(uint(lineLengthRight) % 2u);
    float dampenEffect =
        clamp(float(lineLengthLeft + lineLengthRight - shapeQualityScore) * c_dampeningEffect, 0.0, 1.0);

    float loopFrom = -floor((lineLengthLeft + 1.0) / 2.0) + 1.0;
    float loopTo   = floor((lineLengthRight + 1.0) / 2.0);

    uint blendItemCount = uint(loopTo - loopFrom + 1.0);
    uint itemIndex      = atomicAdd(g_groupSharedBlendItemCount, blendItemCount);

    if ((itemIndex + blendItemCount) > CMAA2_BLEND_ITEM_SLM_SIZE) return false;

    float totalLength = (loopTo - loopFrom) + 1.0 - leftOdd - rightOdd;
    float lerpStep    = 1.0 / totalLength;
    float lerpFromK   = (0.5 - leftOdd - loopFrom) * lerpStep;

    uint itemHeader    = (uint(screenPos.x) << 18u) | uint(screenPos.y);
    uint itemValStatic = (uint(horizontal) << 31u) | (uint(invertedZShape) << 30u);

    for (float i = loopFrom; i <= loopTo; i += 1.0) {
        float secondPart = (i > 0.0) ? 1.0 : 0.0;
        float srcOffset  = 1.0 - secondPart * 2.0;
        float lerpK      = ((lerpStep * i + lerpFromK) * srcOffset + secondPart) * dampenEffect;

        ivec2 encodedItem;
        encodedItem.x = int(itemHeader);
        encodedItem.y =
            int(itemValStatic | ((uint(i + 256.0)) << 20u) | ((uint(srcOffset + 256.0)) << 10u)
                | uint(clamp(lerpK, 0.0, 1.0) * 1023.0 + 0.5));

        g_groupSharedBlendItems[itemIndex++] = uvec2(uint(encodedItem.x), uint(encodedItem.y));
    }
    return true;
}
#endif

void BlendZs(
    ivec2 screenPos,
    bool horizontal,
    bool invertedZShape,
    float shapeQualityScore,
    float lineLengthLeft,
    float lineLengthRight,
    vec2 stepRight)
{
    vec2 blendDir = horizontal ? vec2(0.0, -1.0) : vec2(-1.0, 0.0);
    if (invertedZShape) blendDir = -blendDir;

    float leftOdd  = c_symmetryCorrectionOffset * float(uint(lineLengthLeft) % 2u);
    float rightOdd = c_symmetryCorrectionOffset * float(uint(lineLengthRight) % 2u);
    float dampenEffect =
        clamp(float(lineLengthLeft + lineLengthRight - shapeQualityScore) * c_dampeningEffect, 0.0, 1.0);

    float loopFrom = -floor((lineLengthLeft + 1.0) / 2.0) + 1.0;
    float loopTo   = floor((lineLengthRight + 1.0) / 2.0);

    float totalLength = (loopTo - loopFrom) + 1.0 - leftOdd - rightOdd;
    float lerpStep    = 1.0 / totalLength;
    float lerpFromK   = (0.5 - leftOdd - loopFrom) * lerpStep;

    for (float i = loopFrom; i <= loopTo; i += 1.0) {
        float secondPart = (i > 0.0) ? 1.0 : 0.0;
        float srcOffset  = 1.0 - secondPart * 2.0;
        float lerpK      = ((lerpStep * i + lerpFromK) * srcOffset + secondPart) * dampenEffect;

        ivec2 pixelPos    = screenPos + ivec2(stepRight * i);
        vec3 colorCenter  = LoadSourceColor(pixelPos, ivec2(0));
        vec3 colorFrom    = LoadSourceColor(pixelPos + ivec2(blendDir * srcOffset), ivec2(0));
        vec3 blendedColor = mix(colorCenter, colorFrom, lerpK);

        StoreColorSample(pixelPos, blendedColor, true);
    }
}

void main()
{
#if CMAA2_COLLECT_EXPAND_BLEND_ITEMS
    if (gl_LocalInvocationID.x == 0u) g_groupSharedBlendItemCount = 0u;
    memoryBarrierShared();
    barrier();
#endif

    uint numCandidates = g_workingControlBuffer.data[CTRL_ITEM_COUNT];
    if (gl_GlobalInvocationID.x >= numCandidates) return;

    uint pixelID   = g_workingShapeCandidates.data[gl_GlobalInvocationID.x];
    ivec2 pixelPos = ivec2(int(pixelID >> 18u), int(pixelID & 0x3FFFu));

    uint edgesCenterPacked = LoadEdge(pixelPos, ivec2(0));
    vec4 edges             = UnpackEdgesFlt(edgesCenterPacked);
    vec4 edgesLeft         = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(-1, 0)));
    vec4 edgesRight        = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(1, 0)));
    vec4 edgesBottom       = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, 1)));
    vec4 edgesTop          = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, -1)));

    // Simple shapes
    {
        vec4 blendVal       = ComputeSimpleShapeBlendValues(edges, edgesLeft, edgesRight, edgesTop, edgesBottom);
        float fourWeightSum = dot(blendVal, vec4(1.0));
        float centerWeight  = 1.0 - fourWeightSum;

        vec3 outColor = LoadSourceColor(pixelPos, ivec2(0)) * centerWeight;

        if (blendVal.x > 0.0) outColor += blendVal.x * LoadSourceColor(pixelPos, ivec2(-1, 0));
        if (blendVal.y > 0.0) outColor += blendVal.y * LoadSourceColor(pixelPos, ivec2(0, -1));
        if (blendVal.z > 0.0) outColor += blendVal.z * LoadSourceColor(pixelPos, ivec2(1, 0));
        if (blendVal.w > 0.0) outColor += blendVal.w * LoadSourceColor(pixelPos, ivec2(0, 1));

        StoreColorSample(pixelPos, outColor, false);
    }

    // Complex shapes (Z detection)
    {
        float invertedZScore, normalZScore, maxScore;
        bool horizontal = true;
        bool invertedZ  = false;

        // Horizontal
        {
            vec4 edgesM1P0 = edgesLeft;
            vec4 edgesP1P0 = edgesRight;
            vec4 edgesP2P0 = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(2, 0)));
            DetectZsHorizontal(edges, edgesM1P0, edgesP1P0, edgesP2P0, invertedZScore, normalZScore);
            maxScore = max(invertedZScore, normalZScore);
            if (maxScore > 0.0) invertedZ = invertedZScore > normalZScore;
        }

        // Vertical (rotate 90 degrees counter-clockwise)
        {
            vec4 edgesM1P0 = edgesBottom;
            vec4 edgesP1P0 = edgesTop;
            vec4 edgesP2P0 = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, -2)));
            float invZ, normZ;
            DetectZsHorizontal(edges.argb, edgesM1P0.argb, edgesP1P0.argb, edgesP2P0.argb, invZ, normZ);
            float vertScore = max(invZ, normZ);
            if (vertScore > maxScore) {
                maxScore   = vertScore;
                horizontal = false;
                invertedZ  = invZ > normZ;
            }
        }

        if (maxScore > 0.0) {
            float rawScore          = clamp(4.0 - maxScore, 0.0, 3.0);
            float shapeQualityScore = (cmaa2ExtraSharpness != 0) ? round(rawScore) : floor(rawScore);

            vec2 stepRight = horizontal ? vec2(1.0, 0.0) : vec2(0.0, -1.0);
            float lineLengthLeft, lineLengthRight;
            FindZLineLengths(lineLengthLeft, lineLengthRight, pixelPos, horizontal, invertedZ, stepRight);

            lineLengthLeft -= shapeQualityScore;
            lineLengthRight -= shapeQualityScore;

            if ((lineLengthLeft + lineLengthRight) >= cmaa2MinShapeLength) {
#if CMAA2_COLLECT_EXPAND_BLEND_ITEMS
                if (!CollectBlendZs(
                        pixelPos, horizontal, invertedZ, shapeQualityScore, lineLengthLeft, lineLengthRight, stepRight))
#endif
                    BlendZs(
                        pixelPos, horizontal, invertedZ, shapeQualityScore, lineLengthLeft, lineLengthRight, stepRight);
            }
        }
    }

#if CMAA2_COLLECT_EXPAND_BLEND_ITEMS
    memoryBarrierShared();
    barrier();

    uint totalItemCount = min(uint(CMAA2_BLEND_ITEM_SLM_SIZE), g_groupSharedBlendItemCount);
    uint loops          = (totalItemCount + CMAA2_PROCESS_CANDIDATES_NUM_THREADS - 1u - gl_LocalInvocationID.x)
                          / CMAA2_PROCESS_CANDIDATES_NUM_THREADS;

    for (uint loop = 0u; loop < loops; loop++) {
        uint index    = loop * CMAA2_PROCESS_CANDIDATES_NUM_THREADS + gl_LocalInvocationID.x;
        uvec2 itemVal = g_groupSharedBlendItems[index];

        ivec2 startingPos   = ivec2(int(itemVal.x >> 18u), int(itemVal.x & 0x3FFFu));
        bool itemHorizontal = ((itemVal.y >> 31u) & 1u) != 0u;
        bool itemInvertedZ  = ((itemVal.y >> 30u) & 1u) != 0u;
        float itemStepIndex = float((itemVal.y >> 20u) & 0x3FFu) - 256.0;
        float itemSrcOffset = float((itemVal.y >> 10u) & 0x3FFu) - 256.0;
        float itemLerpK     = float(itemVal.y & 0x3FFu) / 1023.0;

        vec2 itemStepRight = itemHorizontal ? vec2(1.0, 0.0) : vec2(0.0, -1.0);
        vec2 itemBlendDir  = itemHorizontal ? vec2(0.0, -1.0) : vec2(-1.0, 0.0);
        if (itemInvertedZ) itemBlendDir = -itemBlendDir;

        ivec2 itemPixelPos = startingPos + ivec2(itemStepRight * itemStepIndex);
        vec3 colorCenter   = LoadSourceColor(itemPixelPos, ivec2(0));
        vec3 colorFrom     = LoadSourceColor(itemPixelPos + ivec2(itemBlendDir * itemSrcOffset), ivec2(0));
        vec3 outputColor   = mix(colorCenter, colorFrom, itemLerpK);

        StoreColorSample(itemPixelPos, outputColor, true);
    }
#endif
}
