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
    'nit_calibration.frag.glsl',
    'auto_hdr_accumulate.comp.glsl',
    'auto_hdr_reduce.comp.glsl',
    'hdr_debug_pattern.comp.glsl',
]

# 1. Find the required external programs
glsl_compiler   = find_program('glslang', 'glslangValidator')
spirv_optimizer = find_program('spirv-opt')

# 2. Resolve the SPIR-V/Vulkan target environment
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

# 3. Common includes that shaders depend on. 
# Ensures rebuilds trigger when these shared headers change.
shader_includes = files(
    'color_space.h',
    'smaa_settings.h',
    'smaa.h',
    'fxaa3_11.h',
)

# 4. Build the list of expected output headers
shader_h_names = []
foreach shader : shader_src
    shader_h_names += shader.replace('.glsl', '.h')
endforeach
shader_outputs = shader_h_names + ['shader_dict.h']

# 5. Compress textures
compress_textures_script = find_program('compress_textures.sh')

texture_data_cpp = custom_target('texture_data_cpp',
    input : ['AreaTex.h', 'SearchTex.h'],
    output : ['texture_data.cpp'],
    command : [
        compress_textures_script,
        zstd_cli.full_path(),
        '@OUTDIR@',
        '@INPUT0@',
        '@INPUT1@'
    ],
)

# 6. Single custom_target replaces the per file generator. This allows the script to train a shared zstd dictionary for all SPIR-V files.
shader_include = custom_target('shader_headers',
    input : shader_src,
    output : shader_outputs,
    command : [
        find_program('build_shaders.sh'),
        glsl_compiler.full_path(),
        spirv_optimizer.full_path(),
        spirv_target,
        zstd_cli.full_path(),
        '@OUTDIR@',
        '@INPUT@'
    ],
    depend_files : shader_includes,
)
