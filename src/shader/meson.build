shader_src = [
    'cas.frag.glsl',
    'clarity.frag.glsl',
    'claritycas.frag.glsl',
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
]

# Find the modern 'glslang' binary, falling back to 'glslangValidator' for older systems
glsl_compiler = find_program('glslang', 'glslangValidator')

glsl_generator = generator(glsl_compiler,
    output    : [ '@BASENAME@.h' ],
    # -V : Generate SPIR-V
    # -x : Output as C-array hex header
    # --lto : Enable Link-Time Optimization
    arguments : [ '-V', '-x', '--lto', '@INPUT@', '-o', '@OUTPUT@' ])

shader_include = glsl_generator.process(shader_src)
