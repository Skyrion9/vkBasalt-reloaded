#pragma once
#include "shader_decompress.hpp"

namespace vkBasalt
{
    extern const CompressedShader cas_frag;
    extern const CompressedShader clarity_frag;
    extern const CompressedShader clarityrcas_frag;
    extern const CompressedShader crystalclear_frag;
    extern const CompressedShader deband_frag;
    extern const CompressedShader dls_frag;
    extern const CompressedShader full_screen_triangle_vert;
    extern const CompressedShader fxaa_frag;
    extern const CompressedShader lut_frag;
    extern const CompressedShader smaa_blend_frag;
    extern const CompressedShader smaa_blend_vert;
    extern const CompressedShader smaa_edge_color_frag;
    extern const CompressedShader smaa_edge_luma_frag;
    extern const CompressedShader smaa_edge_vert;
    extern const CompressedShader smaa_neighbor_frag;
    extern const CompressedShader smaa_neighbor_vert;
    extern const CompressedShader compute_test_comp;
    extern const CompressedShader frame_accumulate_comp;
    extern const CompressedShader frame_resolve_comp;
    extern const CompressedShader nit_calibration_frag;
    extern const CompressedShader auto_hdr_accumulate_comp;
    extern const CompressedShader auto_hdr_reduce_comp;
    extern const CompressedShader hdr_debug_pattern_comp;
} // namespace vkBasalt
