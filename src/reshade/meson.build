reshade_src = [
    'effect_codegen_spirv.cpp',
    'effect_expression.cpp',
    'effect_lexer.cpp',
    'effect_parser.cpp',
    'effect_preprocessor.cpp',
    'effect_symbol_table.cpp',
]

# Suppress warnings that are common in ReShade's auto-generated lexer/parser code
cpp_args_reshade = cpp.get_supported_arguments([
    '-Wno-unknown-pragmas',
    '-Wno-unused-parameter',
    '-Wno-sign-compare'
])

# This static library will automatically inherit:
# - cpp_std=c++20
# - b_ndebug=if-release (strips debug asserts from the ReShade parser)
# - b_lto=true & b_lto_mode=thin (allows cross-module inlining with the main vkBasalt binary)
reshade_lib = static_library('reshade', reshade_src,
    include_directories : vkBasalt_include_path,
    cpp_args : cpp_args_reshade,
)

reshade_dep = declare_dependency(
    link_with : [ reshade_lib ]
)
