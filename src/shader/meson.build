shader_src = [
    'cas.frag.glsl',
    'clarity.frag.glsl',
    'clarityrcas.frag.glsl',
    'crystalclear.frag.glsl',
    'deband.frag.glsl',
    'dls.frag.glsl',
    'full_screen_triangle.vert.glsl',
    'fxaa.frag.glsl',
    'lut.frag.glsl',
    'smaa_blend.frag.glsl',
    'smaa_blend.vert.glsl',
    'smaa_edge_color.frag.glsl',
    'smaa_edge_luma.frag.glsl',
    'smaa_edge.vert.glsl',
    'smaa_neighbor.frag.glsl',
    'smaa_neighbor.vert.glsl',
    'compute_test.comp.glsl',
    'frame_accumulate.comp.glsl',
    'frame_resolve.comp.glsl',
]

glsl_compiler   = find_program('glslang', 'glslangValidator')
spirv_optimizer = find_program('spirv-opt')
glsl_to_header  = find_program('glsl_to_header.sh')

# --- Resolve the SPIR-V/Vulkan target environment ---
spirv_target_opt = get_option('spirv_target')

if spirv_target_opt == 'auto'
    # Probe glslang from highest to lowest; keep the first target it accepts.
    probe_shader    = meson.current_source_dir() / 'probe.vert.glsl'
    spirv_target    = ''
    candidate_list  = ['vulkan1.4', 'vulkan1.3', 'vulkan1.2', 'vulkan1.1', 'vulkan1.0']

    foreach candidate : candidate_list
        if spirv_target == ''
            probe = run_command(glsl_compiler, '--target-env', candidate, '-V',
                                probe_shader, '-o', '/dev/null', check: false)
            if probe.returncode() == 0
                spirv_target = candidate
            endif
        endif
    endforeach

    if spirv_target == ''
        spirv_target = 'vulkan1.0'
        warning('Could not probe a supported glslang target; falling back to ' + spirv_target)
    endif
else
    spirv_target = spirv_target_opt
endif

message('Shader SPIR-V target environment: ' + spirv_target)

glsl_generator = generator(glsl_to_header,
    output    : [ '@BASENAME@.h' ],
    arguments : [ glsl_compiler.full_path(), spirv_optimizer.full_path(), spirv_target, '@INPUT@', '@OUTPUT@' ])

shader_include = glsl_generator.process(shader_src)
