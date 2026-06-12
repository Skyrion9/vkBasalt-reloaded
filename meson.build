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
