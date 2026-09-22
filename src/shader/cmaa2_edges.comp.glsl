#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_ballot : enable

#define CMAA2_SHADER_EDGES 1
// Cache routing: this shader only writes to edges, candidates, and blend lists.
#define CMAA2_QUAL_EDGE       writeonly
#define CMAA2_QUAL_SHAPE_CAND writeonly
#define CMAA2_QUAL_BLEND_LOC  writeonly
#define CMAA2_QUAL_BLEND_ITEM writeonly
#define CMAA2_QUAL_OUT        writeonly
// Heads and Control remain read write (atomics)

#include "cmaa2_common.glsl"

layout(local_size_x = CMAA2_CS_INPUT_KERNEL_SIZE_X, local_size_y = CMAA2_CS_INPUT_KERNEL_SIZE_Y, local_size_z = 1) in;

// Groupshared edge strengths per 2x2 quad
shared vec4 g_groupShared2x2FracEdgesH[CMAA2_CS_INPUT_KERNEL_SIZE_X * CMAA2_CS_INPUT_KERNEL_SIZE_Y];
shared vec4 g_groupShared2x2FracEdgesV[CMAA2_CS_INPUT_KERNEL_SIZE_X * CMAA2_CS_INPUT_KERNEL_SIZE_Y];

void GroupsharedLoadQuadHV(uint addr, out vec2 e00, out vec2 e10, out vec2 e01, out vec2 e11)
{
    vec4 valH = g_groupShared2x2FracEdgesH[addr];
    e00.y     = valH.x;
    e10.y     = valH.y;
    e01.y     = valH.z;
    e11.y     = valH.w;
    vec4 valV = g_groupShared2x2FracEdgesV[addr];
    e00.x     = valV.x;
    e10.x     = valV.y;
    e01.x     = valV.z;
    e11.x     = valV.w;
}

float ComputeLocalContrastV(int x, int y, vec2 neighbourhood[4][4])
{
    return max(max(neighbourhood[x + 1][y + 0].y, neighbourhood[x + 1][y + 1].y),
               max(neighbourhood[x + 2][y + 0].y, neighbourhood[x + 2][y + 1].y))
           * cmaa2LocalContrastAdaptation;
}

float ComputeLocalContrastH(int x, int y, vec2 neighbourhood[4][4])
{
    return max(max(neighbourhood[x + 0][y + 1].x, neighbourhood[x + 1][y + 1].x),
               max(neighbourhood[x + 0][y + 2].x, neighbourhood[x + 1][y + 2].x))
           * cmaa2LocalContrastAdaptation;
}

vec2 ComputeEdgeLuma(int x, int y, float pixelLumas[8])
{
    vec2 temp;
    temp.x = abs(pixelLumas[x + y * 3] - pixelLumas[x + 1 + y * 3]);
    temp.y = abs(pixelLumas[x + y * 3] - pixelLumas[x + (y + 1) * 3]);
    return temp;
}

vec2 ComputeEdgeColor(int x, int y, vec3 pixelColors[8])
{
    vec2 temp;
    vec3 diffH = abs(pixelColors[x + y * 3] - pixelColors[x + 1 + y * 3]);
    vec3 diffV = abs(pixelColors[x + y * 3] - pixelColors[x + (y + 1) * 3]);
    // Rec.709 is more perceptually accurate for modern displays than the reference's Rec.601
    temp.x = dot(diffH, LUMA_REC709);
    temp.y = dot(diffV, LUMA_REC709);
    return temp;
}

void main()
{
    // Screen position in the input (expanded) kernel (shifted one 2x2 block up/left)
    ivec2 pixelPos = ivec2(gl_WorkGroupID.xy) * ivec2(CMAA2_CS_OUTPUT_KERNEL_SIZE_X, CMAA2_CS_OUTPUT_KERNEL_SIZE_Y)
                     + ivec2(gl_LocalInvocationID.xy) - ivec2(1, 1);
    pixelPos *= ivec2(2, 2);

    const ivec2 qeOffsets[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
    const uint rowStride2x2  = CMAA2_CS_INPUT_KERNEL_SIZE_X;
    const uint centerAddr2x2 = gl_LocalInvocationID.x + gl_LocalInvocationID.y * rowStride2x2;

    const bool inOutputKernel =
        gl_LocalInvocationID.x != (CMAA2_CS_INPUT_KERNEL_SIZE_X - 1) && gl_LocalInvocationID.x != 0
        && gl_LocalInvocationID.y != (CMAA2_CS_INPUT_KERNEL_SIZE_Y - 1) && gl_LocalInvocationID.y != 0;

    vec2 qe0, qe1, qe2, qe3;
    uint[4] outEdges = uint[4](0u, 0u, 0u, 0u);

    if (cmaa2EdgeDetectionMode == 1) {
        // Path 0: Color-based edge detection (catches chromatic transitions luma misses)
        vec3 pixelColors[8];
        for (uint i = 0u; i < 8u; i++) {
            vec3 color     = LoadSourceColor(pixelPos, ivec2(int(i % 3u), int(i / 3u)));
            pixelColors[i] = getEdgeColor(color);
        }
        qe0 = ComputeEdgeColor(0, 0, pixelColors);
        qe1 = ComputeEdgeColor(1, 0, pixelColors);
        qe2 = ComputeEdgeColor(0, 1, pixelColors);
        qe3 = ComputeEdgeColor(1, 1, pixelColors);
    } else {
        // Path 1: Inplace Luma-based edge detection (default, faster intel recommendation)
        float pixelLumas[8];
        for (uint i = 0u; i < 8u; i++) {
            vec3 color    = LoadSourceColor(pixelPos, ivec2(int(i % 3u), int(i / 3u)));
            pixelLumas[i] = getEdgeLuma(color);
        }
        qe0 = ComputeEdgeLuma(0, 0, pixelLumas);
        qe1 = ComputeEdgeLuma(1, 0, pixelLumas);
        qe2 = ComputeEdgeLuma(0, 1, pixelLumas);
        qe3 = ComputeEdgeLuma(1, 1, pixelLumas);
    }
    if (cmaa2EdgeDetectionMode == 1) {
        // Path 0: Color-based edge detection (catches chromatic transitions luma misses)
        vec3 pixelColors[8];
        for (uint i = 0u; i < 8u; i++) {
            vec3 color     = LoadSourceColor(pixelPos, ivec2(int(i % 3u), int(i / 3u)));
            pixelColors[i] = getEdgeColor(color);
        }
        qe0 = ComputeEdgeColor(0, 0, pixelColors);
        qe1 = ComputeEdgeColor(1, 0, pixelColors);
        qe2 = ComputeEdgeColor(0, 1, pixelColors);
        qe3 = ComputeEdgeColor(1, 1, pixelColors);
    } else {
        // Path 1: Inplace Luma-based edge detection (default, faster intel recommendation)
        float pixelLumas[8];
        for (uint i = 0u; i < 8u; i++) {
            vec3 color    = LoadSourceColor(pixelPos, ivec2(int(i % 3u), int(i / 3u)));
            pixelLumas[i] = getEdgeLuma(color);
        }
        qe0 = ComputeEdgeLuma(0, 0, pixelLumas);
        qe1 = ComputeEdgeLuma(1, 0, pixelLumas);
        qe2 = ComputeEdgeLuma(0, 1, pixelLumas);
        qe3 = ComputeEdgeLuma(1, 1, pixelLumas);
    }

    g_groupShared2x2FracEdgesV[centerAddr2x2] = vec4(qe0.x, qe1.x, qe2.x, qe3.x);
    g_groupShared2x2FracEdgesH[centerAddr2x2] = vec4(qe0.y, qe1.y, qe2.y, qe3.y);

    memoryBarrierShared();
    barrier();

    if (inOutputKernel) {
        // Clear deferred color list heads for every quad in the output kernel. The union of all groups' output kernels covers
        // every on screen quad only once. Kept inside inOutputKernel because border threads' coords alias neighboring groups' quads.
        uvec2 headsDims  = (uvec2(resX, resY) + uvec2(1, 1)) / uvec2(2, 2);
        ivec2 headsCoord = pixelPos / 2;
        if (uint(headsCoord.x) < headsDims.x && uint(headsCoord.y) < headsDims.y) {
            imageStore(g_workingDeferredBlendItemListHeads, headsCoord, uvec4(0xFFFFFFFFu, 0u, 0u, 1u));
        }
        vec2 topRow           = g_groupShared2x2FracEdgesH[centerAddr2x2 - rowStride2x2].zw;
        vec2 leftColumn       = g_groupShared2x2FracEdgesV[centerAddr2x2 - 1u].yw;
        bool someNonZeroEdges = any(notEqual(qe0, vec2(0.0))) || any(notEqual(qe1, vec2(0.0)))
                                || any(notEqual(qe2, vec2(0.0))) || any(notEqual(qe3, vec2(0.0))) || topRow.x != 0.0
                                || topRow.y != 0.0 || leftColumn.x != 0.0 || leftColumn.y != 0.0;

        if (someNonZeroEdges) {
            vec4 ce[4];

            // Local contrast adaptation
            vec2 neighbourhood[4][4];
            vec2 d0, d1, d2;

            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2 - 1u, d0, d1, d2, neighbourhood[0][0]);
            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2, d0, d1, neighbourhood[1][0], neighbourhood[2][0]);
            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2 + 1u, d0, d1, neighbourhood[3][0], d2);
            GroupsharedLoadQuadHV(centerAddr2x2 - 1u, d0, neighbourhood[0][1], d1, neighbourhood[0][2]);
            GroupsharedLoadQuadHV(centerAddr2x2 + 1u, neighbourhood[3][1], d0, neighbourhood[3][2], d1);
            GroupsharedLoadQuadHV(centerAddr2x2 - 1u + rowStride2x2, d0, neighbourhood[0][3], d1, d2);
            GroupsharedLoadQuadHV(centerAddr2x2 + rowStride2x2, neighbourhood[1][3], neighbourhood[2][3], d0, d1);

            neighbourhood[1][0].y = topRow.x;
            neighbourhood[2][0].y = topRow.y;
            neighbourhood[0][1].x = leftColumn.x;
            neighbourhood[0][2].x = leftColumn.y;
            neighbourhood[1][1]   = qe0;
            neighbourhood[2][1]   = qe1;
            neighbourhood[1][2]   = qe2;
            neighbourhood[2][2]   = qe3;

            topRow.x = (topRow.x - ComputeLocalContrastH(0, -1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            topRow.y = (topRow.y - ComputeLocalContrastH(1, -1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            leftColumn.x =
                (leftColumn.x - ComputeLocalContrastV(-1, 0, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            leftColumn.y =
                (leftColumn.y - ComputeLocalContrastV(-1, 1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;

            ce[0].x = (qe0.x - ComputeLocalContrastV(0, 0, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[0].y = (qe0.y - ComputeLocalContrastH(0, 0, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[1].x = (qe1.x - ComputeLocalContrastV(1, 0, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[1].y = (qe1.y - ComputeLocalContrastH(1, 0, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[2].x = (qe2.x - ComputeLocalContrastV(0, 1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[2].y = (qe2.y - ComputeLocalContrastH(0, 1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[3].x = (qe3.x - ComputeLocalContrastV(1, 1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;
            ce[3].y = (qe3.y - ComputeLocalContrastH(1, 1, neighbourhood)) > cmaa2EdgeThreshold ? 1.0 : 0.0;

            // Left edges
            ce[0].z = leftColumn.x;
            ce[1].z = ce[0].x;
            ce[2].z = leftColumn.y;
            ce[3].z = ce[2].x;

            // Top edges
            ce[0].w = topRow.x;
            ce[1].w = topRow.y;
            ce[2].w = ce[0].y;
            ce[3].w = ce[1].y;

            for (uint i = 0u; i < 4u; i++) {
                ivec2 localPixelPos = pixelPos + qeOffsets[i];
                vec4 edges          = ce[i];

                // Shape candidate: at least one two edge corner
                bool isCandidate =
                    (edges.x * edges.y + edges.y * edges.z + edges.z * edges.w + edges.w * edges.x) != 0.0;

                // Subgroup atomic reduction: reduces global atomic contention by up to 64x on RDNA2 etc.
                // Instead of every thread firing an atomicAdd, the wave counts its candidates,
                // the first active lane does one global atomicAdd for the whole wave, and then we
                // distribute the indices using a wave level prefix sum (exclusive bit count).
                uvec4 ballot   = subgroupBallot(isCandidate);
                uint waveCount = subgroupBallotBitCount(ballot);

                uint waveBase = 0u;
                if (subgroupElect()) {
                    if (waveCount > 0u) {
                        waveBase = atomicAdd(g_workingControlBuffer.data[CTRL_SHAPE_CANDIDATES], waveCount);
                    }
                }
                waveBase = subgroupBroadcastFirst(waveBase);

                if (isCandidate) {
                    uint laneOffset   = subgroupBallotExclusiveBitCount(ballot);
                    uint counterIndex = waveBase + laneOffset;

                    // Vulkan SSBO bounds check: prevent heap memory corruption on dense edge scenes
                    if (counterIndex < g_workingShapeCandidates.data.length()) {
                        g_workingShapeCandidates.data[counterIndex] =
                            (uint(localPixelPos.x) << 18u) | uint(localPixelPos.y);
                    }
                }

                outEdges[i] = PackEdges(edges);
            }
        }
    }

    // Write edges (R8_UINT, two pixels packed into one 8 bit texel). Must be restricted to inOutputKernel. The output kernels of all threadgroups
    // tile the screen only once with no overlap. Border threads' coordinates alias the neighboring groups' output quads (14g-1 and 14g+14),
    // so writing outside this guard would cause overlapping writes and corrupt the packed edge masks.
    if (inOutputKernel) {
        uint logicalWidth = resX;
        ivec2 edgeCoord0  = ivec2(pixelPos.x / 2, pixelPos.y + 0);
        ivec2 edgeCoord1  = ivec2(pixelPos.x / 2, pixelPos.y + 1);
        if (uint(edgeCoord0.x * 2) < logicalWidth && uint(edgeCoord0.y) < resY) {
            imageStore(g_workingEdges, edgeCoord0, uvec4((outEdges[1] << 4) | outEdges[0], 0u, 0u, 1u));
        }
        if (uint(edgeCoord1.x * 2) < logicalWidth && uint(edgeCoord1.y) < resY) {
            imageStore(g_workingEdges, edgeCoord1, uvec4((outEdges[3] << 4) | outEdges[2], 0u, 0u, 1u));
        }
    }
}
