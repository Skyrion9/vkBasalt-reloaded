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
    'crystalclear_presets.cpp',
    'crystalclear_params.cpp',
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
    'simple_compute_pass.cpp',
    'compute_test_pass.cpp',
    'frame_analyzer.cpp',
    'hdr_detect.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'imgui_overlay.cpp',
    'overlay_manager.cpp',
    'imgui_stats_tab.cpp',
    'imgui_style_tab.cpp',
    'imgui_presets_tab.cpp',
    'imgui_settings_tab.cpp',
    'imgui_shaders_tab.cpp',
    'imgui_theme.cpp',
    'game_detect.cpp',
    'keyboard_input.cpp',
    'keyboard_input_wayland.cpp',
    'hotkey_manager.cpp',
    'pipeline_cache.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'reshade_uniforms.cpp',
    'sampler.cpp',
    'shader.cpp',
    'screenshot.cpp',
    'color_math.cpp',
    'stb_image.c',
    'stb_image_resize.c',
    'stb_image_write.c',
    'util.cpp',
    'vkdispatch.cpp',
]

x11_dep = dependency('x11')
wayland_client_dep = dependency('wayland-client')
xkbcommon_dep = dependency('xkbcommon')
vulkan_dep = dependency('vulkan')
tinyexr_dep = subproject('tinyexr').get_variable('tinyexr_dep')

# --- Wayland Protocol Generation ---
wayland_protocols_dep = dependency('wayland-protocols')
wayland_scanner_dep = dependency('wayland-scanner')

wayland_scanner = find_program(wayland_scanner_dep.get_variable('wayland_scanner'))
wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')

relative_pointer_xml = wl_protocol_dir / 'unstable/relative-pointer/relative-pointer-unstable-v1.xml'

relative_pointer_header = custom_target('relative-pointer-client-header',
  input: relative_pointer_xml,
  output: 'relative-pointer-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

relative_pointer_code = custom_target('relative-pointer-private-code',
  input: relative_pointer_xml,
  output: 'relative-pointer-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# Add generated files to sources
vkBasalt_src += [relative_pointer_header, relative_pointer_code]
# -----------------------------------

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

shared_library(meson.project_name().to_lower(), 
    vkBasalt_src, shader_include,
    link_with: [keyboard_input_x11_lib],
    include_directories : vkBasalt_include_path,
    dependencies : [x11_dep, reshade_dep, wayland_client_dep, xkbcommon_dep, imgui_dep, vulkan_dep, tinyexr_dep],
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)
