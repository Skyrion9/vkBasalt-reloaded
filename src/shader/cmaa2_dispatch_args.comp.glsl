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

#define CMAA2_SHADER_DISPATCH 1
// Cache routing: reads candidates/locations for bounds, writes execute indirect.
#define CMAA2_QUAL_SHAPE_CAND readonly
#define CMAA2_QUAL_BLEND_LOC  readonly
#define CMAA2_QUAL_EXECUTE    writeonly
// Control remains read/write (reads counts, writes cleared counts)

#include "cmaa2_common.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    // Activated on Dispatch(2, 1, 1): compute args for ProcessCandidatesCS
    if (gl_WorkGroupID.x == 1u) {
        uint shapeCandidateCount = g_workingControlBuffer.data[CTRL_SHAPE_CANDIDATES];

        // Clamp to buffer capacity
        shapeCandidateCount = min(shapeCandidateCount, uint(g_workingShapeCandidates.data.length()));

        // Write indirect dispatch arguments for ProcessCandidatesCS
        g_workingExecuteIndirectBuffer.data[0] =
            (shapeCandidateCount + CMAA2_PROCESS_CANDIDATES_NUM_THREADS - 1u) / CMAA2_PROCESS_CANDIDATES_NUM_THREADS;
        g_workingExecuteIndirectBuffer.data[1] = 1u;
        g_workingExecuteIndirectBuffer.data[2] = 1u;

        // Write actual item count for ProcessCandidatesCS to read
        g_workingControlBuffer.data[CTRL_ITEM_COUNT] = shapeCandidateCount;
    }
    // Activated on Dispatch(1, 2, 1): compute args for DeferredColorApply2x2CS
    else if (gl_WorkGroupID.y == 1u) {
        uint blendLocationCount = g_workingControlBuffer.data[CTRL_BLEND_LOCATIONS];

        // Clamp to buffer capacity
        blendLocationCount = min(blendLocationCount, uint(g_workingDeferredBlendLocationList.data.length()));

// Write indirect dispatch arguments for DeferredColorApply2x2CS
#if CMAA2_DEFERRED_APPLY_THREADGROUP_SWAP
        g_workingExecuteIndirectBuffer.data[0] = 1u;
        g_workingExecuteIndirectBuffer.data[1] =
            (blendLocationCount + CMAA2_DEFERRED_APPLY_NUM_THREADS - 1u) / CMAA2_DEFERRED_APPLY_NUM_THREADS;
#else
        g_workingExecuteIndirectBuffer.data[0] =
            (blendLocationCount + CMAA2_DEFERRED_APPLY_NUM_THREADS - 1u) / CMAA2_DEFERRED_APPLY_NUM_THREADS;
        g_workingExecuteIndirectBuffer.data[1] = 1u;
#endif
        g_workingExecuteIndirectBuffer.data[2] = 1u;

        // Write actual item count for DeferredColorApply2x2CS to read
        g_workingControlBuffer.data[CTRL_ITEM_COUNT] = blendLocationCount;

        // Clear counters for next frame
        g_workingControlBuffer.data[CTRL_SHAPE_CANDIDATES] = 0u;
        g_workingControlBuffer.data[CTRL_BLEND_LOCATIONS]  = 0u;
        g_workingControlBuffer.data[CTRL_BLEND_ITEMS]      = 0u;
    }
}
