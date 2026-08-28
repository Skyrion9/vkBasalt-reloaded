project('tinyexr', 'cpp')

miniz_dep = subproject('miniz').get_variable('miniz_dep')

tinyexr_lib = static_library('tinyexr',
    'tinyexr_impl.cpp',
    dependencies: [miniz_dep],
    include_directories: include_directories('.'),
)

tinyexr_dep = declare_dependency(
    link_with: tinyexr_lib,
    include_directories: include_directories('.'),
)