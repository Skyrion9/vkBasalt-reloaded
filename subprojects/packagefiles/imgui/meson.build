project('imgui', 'cpp', version: '1.92.9b',
    default_options: [
        'cpp_std=c++20',
        'buildtype=release',
        'b_ndebug=if-release',
        'b_lto=true',
        'b_lto_mode=thin',
        'optimization=3',
    ]
)

# Find the system Vulkan loader library
vulkan_dep = dependency('vulkan')

# Core + Vulkan backend
imgui_sources = files(
    'imgui.cpp',
    'imgui_draw.cpp',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
    'imgui_demo.cpp',
    'backends/imgui_impl_vulkan.cpp',
)

imgui_inc = include_directories('.', 'backends')

imgui_args = [
    '-fno-rtti',
    '-fno-semantic-interposition',
    '-fdata-sections',
    '-ffunction-sections',
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wno-implicit-fallthrough',
    '-Wno-conditional-uninitialized',
]

imgui_lib = static_library('imgui',
    imgui_sources,
    include_directories: imgui_inc,
    cpp_args: imgui_args,
)

# Pass the vulkan_dep transitively to anything that depends on imgui
imgui_dep = declare_dependency(
    link_with: imgui_lib,
    include_directories: imgui_inc,
    dependencies: vulkan_dep
)