project('vkBasalt', ['c', 'cpp'], 
    default_options: [
        'c_std=c17', 
        'cpp_std=c++20',
        'buildtype=release',
        'b_ndebug=if-release',
        'b_lto=true',
        'b_lto_mode=thin',
        'optimization=3'
    ]
)

cpp = meson.get_compiler('cpp')

# For a vulkan layer we might not need RTTI. Disabling these probably shrinks the binary, speeds up the dynamic linker, and saves memory.
# Don't use -fno-exceptions we do use exceptions.
# TODO: Have to ensure we don't break reshade which might depend on some of the below.
cpp_args_common = cpp.get_supported_arguments([
    '-fno-rtti',                  # Disables Run-Time Type Information
    '-fvisibility-inlines-hidden',# Hides inline function symbols from the export table
    '-Wno-unknown-pragmas',
    '-Wno-unused-parameter',
    '-Wno-sign-compare'
])

add_project_arguments(cpp_args_common, language : 'cpp')

vkBasalt_include_path = ''

lib_dir = get_option('libdir')
ld_lib_dir_vkbasalt = ''

if get_option('append_libdir_vkbasalt')
    lib_dir = join_paths(lib_dir, 'vkbasalt')
    ld_lib_dir_vkbasalt = get_option('prefix') + '/$LIB/vkbasalt/'
endif

if get_option('with_so')
    subdir('src')
endif

if get_option('with_json')
    subdir('config')
endif
