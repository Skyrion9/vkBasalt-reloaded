project('vkBasalt', ['c', 'cpp'],
    meson_version: '>=1.1.0',
    default_options: [
        'c_std=c17', 
        'cpp_std=c++20',
        'buildtype=release',
        'b_ndebug=if-release',
        'b_lto=true',
        'b_lto_mode=thin',
        'optimization=2'
    ]
)

pgo_mode = get_option('pgo')
pgo_dir  = get_option('pgo_dir')

pgo_args = []
if pgo_mode == 'generate'
    pgo_args = ['-fprofile-generate=' + pgo_dir]
elif pgo_mode == 'use'
    pgo_args = ['-fprofile-use=' + pgo_dir]
endif

if pgo_args.length() > 0
    add_project_arguments(pgo_args, language: ['c', 'cpp'])
    add_project_link_arguments(pgo_args, language: ['c', 'cpp'])
endif

cpp = meson.get_compiler('cpp')
c = meson.get_compiler('c')
imgui_dep = dependency('imgui')
zstd_dep = dependency('libzstd', fallback: ['zstd', 'libzstd_dep'])
zstd_cli = find_program('zstd')

# Shared library hardening + performance flags
cpp_args_common = cpp.get_supported_arguments([
    '-fno-rtti',                    # No RTTI needed for a Vulkan layer
    '-fvisibility-inlines-hidden',  # Hide inline symbols from export table
    '-fno-semantic-interposition',  # Allow direct calls, skip PLT indirection
    '-fno-plt',                     # Eliminate PLT entirely
    '-fdata-sections',              # Allow linker to GC unused data
    '-ffunction-sections',          # Allow linker to GC unused functions
    '-fmerge-all-constants',        # Merge identical constants
    '-fno-ident',                   # Remove compiler ident string
    '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Wno-unknown-pragmas',
    '-Wno-unused-parameter',
    '-Wno-sign-compare'
])

c_args_common = c.get_supported_arguments([
    '-fdata-sections',
    '-ffunction-sections',
    '-fmerge-all-constants',
    '-fno-ident',
    '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables',
    '-fno-math-errno',
    '-fno-trapping-math',
])

# Linker flags: strip, GC dead code, optimize, bind symbols internally
ld_args_common = [
    '-Wl,--gc-sections',       # Strip unused functions/data sections
    '-Wl,-Bsymbolic',          # Bind internal symbols at link time
    '-Wl,-O2',                 # Linker optimization pass
    '-Wl,--as-needed',         # Only link libraries actually used
    '-Wl,--exclude-libs,ALL',  # Prevent symbol leakage from static libs
]

# ICF (Identical Code Folding) merges identical C++ template instantiations to shrink .text.
# Supported by lld, gold, mold etc. GNU bfd does not support --icf.
linker_id = cpp.get_linker_id()
if linker_id in ['ld.lld', 'ld.gold', 'ld.mold']
    ld_args_common += ['-Wl,--icf=all']
endif

add_project_arguments(cpp_args_common, language : 'cpp')
add_project_arguments(c_args_common, language : 'c')
add_project_link_arguments(ld_args_common, language : ['c', 'cpp'])

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
