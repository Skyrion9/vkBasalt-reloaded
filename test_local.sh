#!/bin/bash
# test_local.sh - Run transition tests on your real GPU with strict validation
set -euo pipefail

export ENABLE_VKBASALT_RELOADED=1

# CRITICAL FIX: Include BOTH the local vkBasalt build AND the system layer directories.
# Setting VK_LAYER_PATH to only builddir/src hides the Khronos validation layer manifest,
# which lives in /usr/share/vulkan/explicit_layer.d/.
export VK_LAYER_PATH="$(pwd)/builddir/config:$(pwd)/builddir/src:/usr/share/vulkan/explicit_layer.d${VK_LAYER_PATH:+:$VK_LAYER_PATH}"

export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_LOADER_DEBUG=all
export VKBASALT_LOG_LEVEL=debug
# PROTON_ENABLE_WAYLAND is irrelevant for native vkcube. Remove it.
# The harness in test_runtime_transitions.sh already unsets WAYLAND_DISPLAY to force X11.
export VK_LAYER_SETTINGS_PATH=/tmp/vk_layer_settings.txt

cat > /tmp/vk_layer_settings.txt << 'EOF'
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.report_flags = error,warn,perf,info,verbose
khronos_validation.log_filename = /tmp/validation_errors.log
# Filter out known vkcube synchronization flaws (it doesn't use fences, so it resets
# command buffers while they are pending when the GPU is busy with our effect chain).
khronos_validation.message_id_filter = 0x1e7883ea,0x84029a9f,0x2e2f4d65
EOF

# Clear previous logs
rm -f /tmp/validation_errors.log

# Run the harness (it will detect $DISPLAY and skip Xvfb, and force X11 if on Wayland)
./test_runtime_transitions.sh
