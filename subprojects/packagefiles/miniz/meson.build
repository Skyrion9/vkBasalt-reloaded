project('miniz', 'c')

miniz_lib = static_library('miniz',
    'miniz.c',
    'miniz_tdef.c',
    'miniz_tinfl.c',
    'miniz_zip.c',
    include_directories: include_directories('.'),
    c_args: ['-D_GNU_SOURCE'],
)

miniz_dep = declare_dependency(
    link_with: miniz_lib,
    include_directories: include_directories('.'),
)