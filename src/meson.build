subdir('shader')
subdir('reshade')

vkBasalt_src = [
    'basalt.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'descriptor_set.cpp',
    'effect_cas.cpp',
    'effect_clarity.cpp',
    'effect_clarityrcas.cpp',
    'effect_crystalclear.cpp',
    'effect.cpp',
    'effect_deband.cpp',
    'effect_dls.cpp',
    'effect_fxaa.cpp',
    'effect_lut.cpp',
    'effect_reshade.cpp',
    'effect_simple.cpp',
    'effect_smaa.cpp',
    'effect_transfer.cpp',
    'effect_chain.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'imgui_overlay.cpp',
    'overlay_manager.cpp',
    'imgui_tabs.cpp',
    'imgui_theme.cpp',
    'game_detect.cpp',
    'keyboard_input.cpp',
    'keyboard_input_wayland.cpp',
    'hotkey_manager.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'reshade_uniforms.cpp',
    'sampler.cpp',
    'shader.cpp',
    'stb_image.c',
    'stb_image_resize.c',
    'util.cpp',
    'vkdispatch.cpp',
]

x11_dep = dependency('x11')
wayland_client_dep = dependency('wayland-client')
xkbcommon_dep = dependency('xkbcommon')

conf_paths = configuration_data()
conf_paths.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
conf_paths.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))

configure_file(output: 'config_paths.hpp',
               configuration : conf_paths)

# X11/X.h contains macro definitions (e.g. None) that conflict with the
# rest of the codebase when Meson --unity=on merges translation units.
# Isolating it in a static_library prevents the collision.
keyboard_input_x11_lib = static_library('keyboard_input_x11',
    'keyboard_input_x11.cpp',
    # Override: this file needs X11 macros visible, no RTTI stripping
    cpp_args: ['-Wno-unknown-pragmas'],
    dependencies: [imgui_dep],
)

vulkan_dep = dependency('vulkan')

shared_library(meson.project_name().to_lower(), 
    vkBasalt_src, shader_include,
    link_with: [keyboard_input_x11_lib],
    include_directories : vkBasalt_include_path,
    dependencies : [x11_dep, reshade_dep, wayland_client_dep, xkbcommon_dep, imgui_dep, vulkan_dep],
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)
