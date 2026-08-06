reshade_src = [
    'effect_codegen_spirv.cpp',
    'effect_expression.cpp',
    'effect_lexer.cpp',
    'effect_parser.cpp',
    'effect_preprocessor.cpp',
    'effect_symbol_table.cpp',
]

# Suppress warnings common in ReShade's auto-generated lexer/parser code
cpp_args_reshade = cpp.get_supported_arguments([
    '-Wno-unknown-pragmas',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-deprecated-copy',       # ReShade uses older copy patterns
    '-Wno-class-memaccess',       # memcpy on non-trivial types in lexer
])

# This static library inherits:
# - cpp_std=c++20, b_ndebug, b_lto, optimization=3 from project defaults
# - -fdata-sections, -ffunction-sections for linker GC
# - ThinLTO allows cross-module inlining with the main vkBasalt binary
reshade_lib = static_library('reshade', reshade_src,
    include_directories : vkBasalt_include_path,
    cpp_args : cpp_args_reshade,
)

reshade_dep = declare_dependency(
    link_with : [ reshade_lib ]
)
