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


namespace vkBasalt {
    const CompressedShader cas_frag                        = { cas_frag_zst,                        sizeof(cas_frag_zst),                        cas_frag_spirv_size };
    const CompressedShader clarity_frag                    = { clarity_frag_zst,                    sizeof(clarity_frag_zst),                    clarity_frag_spirv_size };
    const CompressedShader clarityrcas_frag                = { clarityrcas_frag_zst,                sizeof(clarityrcas_frag_zst),                clarityrcas_frag_spirv_size };
    const CompressedShader crystalclear_frag               = { crystalclear_frag_zst,               sizeof(crystalclear_frag_zst),               crystalclear_frag_spirv_size };
    const CompressedShader deband_frag                     = { deband_frag_zst,                     sizeof(deband_frag_zst),                     deband_frag_spirv_size };
    const CompressedShader dls_frag                        = { dls_frag_zst,                        sizeof(dls_frag_zst),                        dls_frag_spirv_size };
    const CompressedShader full_screen_triangle_vert       = { full_screen_triangle_vert_zst,       sizeof(full_screen_triangle_vert_zst),       full_screen_triangle_vert_spirv_size };
    const CompressedShader fxaa_frag                       = { fxaa_frag_zst,                       sizeof(fxaa_frag_zst),                       fxaa_frag_spirv_size };
    const CompressedShader lut_frag                        = { lut_frag_zst,                        sizeof(lut_frag_zst),                        lut_frag_spirv_size };
    const CompressedShader smaa_blend_frag                 = { smaa_blend_frag_zst,                 sizeof(smaa_blend_frag_zst),                 smaa_blend_frag_spirv_size };
    const CompressedShader smaa_blend_vert                 = { smaa_blend_vert_zst,                 sizeof(smaa_blend_vert_zst),                 smaa_blend_vert_spirv_size };
    const CompressedShader smaa_edge_color_frag            = { smaa_edge_color_frag_zst,            sizeof(smaa_edge_color_frag_zst),            smaa_edge_color_frag_spirv_size };
    const CompressedShader smaa_edge_luma_frag             = { smaa_edge_luma_frag_zst,             sizeof(smaa_edge_luma_frag_zst),             smaa_edge_luma_frag_spirv_size };
    const CompressedShader smaa_edge_vert                  = { smaa_edge_vert_zst,                  sizeof(smaa_edge_vert_zst),                  smaa_edge_vert_spirv_size };
    const CompressedShader smaa_neighbor_frag              = { smaa_neighbor_frag_zst,              sizeof(smaa_neighbor_frag_zst),              smaa_neighbor_frag_spirv_size };
    const CompressedShader smaa_neighbor_vert              = { smaa_neighbor_vert_zst,              sizeof(smaa_neighbor_vert_zst),              smaa_neighbor_vert_spirv_size };
    const CompressedShader compute_test_comp               = { compute_test_comp_zst,               sizeof(compute_test_comp_zst),               compute_test_comp_spirv_size };
    const CompressedShader frame_accumulate_comp           = { frame_accumulate_comp_zst,           sizeof(frame_accumulate_comp_zst),           frame_accumulate_comp_spirv_size };
    const CompressedShader frame_resolve_comp              = { frame_resolve_comp_zst,              sizeof(frame_resolve_comp_zst),              frame_resolve_comp_spirv_size };
    const CompressedShader nit_calibration_frag            = { nit_calibration_frag_zst,            sizeof(nit_calibration_frag_zst),            nit_calibration_frag_spirv_size };
    const CompressedShader auto_hdr_accumulate_comp        = { auto_hdr_accumulate_comp_zst,        sizeof(auto_hdr_accumulate_comp_zst),        auto_hdr_accumulate_comp_spirv_size };
    const CompressedShader auto_hdr_reduce_comp            = { auto_hdr_reduce_comp_zst,            sizeof(auto_hdr_reduce_comp_zst),            auto_hdr_reduce_comp_spirv_size };
    const CompressedShader hdr_debug_pattern_comp          = { hdr_debug_pattern_comp_zst,          sizeof(hdr_debug_pattern_comp_zst),          hdr_debug_pattern_comp_spirv_size };
} // namespace vkBasalt
