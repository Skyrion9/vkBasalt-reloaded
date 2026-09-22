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
