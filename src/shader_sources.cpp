#include "shader_sources.hpp"

#include "cas.frag.h"
#include "clarity.frag.h"
#include "clarityrcas.frag.h"
#include "crystalclear.frag.h"
#include "deband.frag.h"
#include "dls.frag.h"
#include "full_screen_triangle.vert.h"
#include "fxaa.frag.h"
#include "lut.frag.h"
#include "smaa_blend.frag.h"
#include "smaa_blend.vert.h"
#include "smaa_edge_color.frag.h"
#include "smaa_edge_luma.frag.h"
#include "smaa_edge.vert.h"
#include "smaa_neighbor.frag.h"
#include "smaa_neighbor.vert.h"
#include "compute_test.comp.h"
#include "frame_accumulate.comp.h"
#include "frame_resolve.comp.h"
#include "nit_calibration.frag.h"
#include "auto_hdr_accumulate.comp.h"
#include "auto_hdr_reduce.comp.h"
#include "hdr_debug_pattern.comp.h"

namespace vkBasalt
{
    const CompressedShader cas_frag = {
        .data = cas_frag_zst, .compressedSize = sizeof(cas_frag_zst), .originalSize = cas_frag_spirv_size};
    const CompressedShader clarity_frag = {
        .data = clarity_frag_zst, .compressedSize = sizeof(clarity_frag_zst), .originalSize = clarity_frag_spirv_size};
    const CompressedShader clarityrcas_frag = {
        .data           = clarityrcas_frag_zst,
        .compressedSize = sizeof(clarityrcas_frag_zst),
        .originalSize   = clarityrcas_frag_spirv_size};
    const CompressedShader crystalclear_frag = {
        .data           = crystalclear_frag_zst,
        .compressedSize = sizeof(crystalclear_frag_zst),
        .originalSize   = crystalclear_frag_spirv_size};
    const CompressedShader deband_frag = {
        .data = deband_frag_zst, .compressedSize = sizeof(deband_frag_zst), .originalSize = deband_frag_spirv_size};
    const CompressedShader dls_frag = {
        .data = dls_frag_zst, .compressedSize = sizeof(dls_frag_zst), .originalSize = dls_frag_spirv_size};
    const CompressedShader full_screen_triangle_vert = {
        .data           = full_screen_triangle_vert_zst,
        .compressedSize = sizeof(full_screen_triangle_vert_zst),
        .originalSize   = full_screen_triangle_vert_spirv_size};
    const CompressedShader fxaa_frag = {
        .data = fxaa_frag_zst, .compressedSize = sizeof(fxaa_frag_zst), .originalSize = fxaa_frag_spirv_size};
    const CompressedShader lut_frag = {
        .data = lut_frag_zst, .compressedSize = sizeof(lut_frag_zst), .originalSize = lut_frag_spirv_size};
    const CompressedShader smaa_blend_frag = {
        .data           = smaa_blend_frag_zst,
        .compressedSize = sizeof(smaa_blend_frag_zst),
        .originalSize   = smaa_blend_frag_spirv_size};
    const CompressedShader smaa_blend_vert = {
        .data           = smaa_blend_vert_zst,
        .compressedSize = sizeof(smaa_blend_vert_zst),
        .originalSize   = smaa_blend_vert_spirv_size};
    const CompressedShader smaa_edge_color_frag = {
        .data           = smaa_edge_color_frag_zst,
        .compressedSize = sizeof(smaa_edge_color_frag_zst),
        .originalSize   = smaa_edge_color_frag_spirv_size};
    const CompressedShader smaa_edge_luma_frag = {
        .data           = smaa_edge_luma_frag_zst,
        .compressedSize = sizeof(smaa_edge_luma_frag_zst),
        .originalSize   = smaa_edge_luma_frag_spirv_size};
    const CompressedShader smaa_edge_vert = {
        .data           = smaa_edge_vert_zst,
        .compressedSize = sizeof(smaa_edge_vert_zst),
        .originalSize   = smaa_edge_vert_spirv_size};
    const CompressedShader smaa_neighbor_frag = {
        .data           = smaa_neighbor_frag_zst,
        .compressedSize = sizeof(smaa_neighbor_frag_zst),
        .originalSize   = smaa_neighbor_frag_spirv_size};
    const CompressedShader smaa_neighbor_vert = {
        .data           = smaa_neighbor_vert_zst,
        .compressedSize = sizeof(smaa_neighbor_vert_zst),
        .originalSize   = smaa_neighbor_vert_spirv_size};
    const CompressedShader compute_test_comp = {
        .data           = compute_test_comp_zst,
        .compressedSize = sizeof(compute_test_comp_zst),
        .originalSize   = compute_test_comp_spirv_size};
    const CompressedShader frame_accumulate_comp = {
        .data           = frame_accumulate_comp_zst,
        .compressedSize = sizeof(frame_accumulate_comp_zst),
        .originalSize   = frame_accumulate_comp_spirv_size};
    const CompressedShader frame_resolve_comp = {
        .data           = frame_resolve_comp_zst,
        .compressedSize = sizeof(frame_resolve_comp_zst),
        .originalSize   = frame_resolve_comp_spirv_size};
    const CompressedShader nit_calibration_frag = {
        .data           = nit_calibration_frag_zst,
        .compressedSize = sizeof(nit_calibration_frag_zst),
        .originalSize   = nit_calibration_frag_spirv_size};
    const CompressedShader auto_hdr_accumulate_comp = {
        .data           = auto_hdr_accumulate_comp_zst,
        .compressedSize = sizeof(auto_hdr_accumulate_comp_zst),
        .originalSize   = auto_hdr_accumulate_comp_spirv_size};
    const CompressedShader auto_hdr_reduce_comp = {
        .data           = auto_hdr_reduce_comp_zst,
        .compressedSize = sizeof(auto_hdr_reduce_comp_zst),
        .originalSize   = auto_hdr_reduce_comp_spirv_size};
    const CompressedShader hdr_debug_pattern_comp = {
        .data           = hdr_debug_pattern_comp_zst,
        .compressedSize = sizeof(hdr_debug_pattern_comp_zst),
        .originalSize   = hdr_debug_pattern_comp_spirv_size};
} // namespace vkBasalt
