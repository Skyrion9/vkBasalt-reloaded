#!/bin/bash
set -euo pipefail

renice -n 19 -p $$ >/dev/null 2>&1 || true
ionice -c 3 -p $$ >/dev/null 2>&1 || true

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "=== Local PGO Profiling on Real GPU ==="
echo ""

rm -rf pgo-data builddir-gen builddir-pgo
mkdir -p pgo-data

export CC=clang
export CXX=clang++
export CC_LD=mold
export CXX_LD=mold

if command -v ccache &>/dev/null; then
    export CC="ccache clang"
    export CXX="ccache clang++"
    echo "ccache detected and enabled."
fi

CONFIG_DIR="$HOME/.config/vkBasalt-reloaded"
CONFIG_PATH="$CONFIG_DIR/vkBasalt-reloaded.conf"
MANIFEST_PATH="$HOME/.local/share/vulkan/implicit_layer.d/vkBasalt.json"
BACKUP_DIR=$(mktemp -d)

cleanup() {
    echo ""
    echo "=== Restoring original files ==="
    if [ -f "$BACKUP_DIR/vkBasalt-reloaded.conf.bak" ]; then
        cp "$BACKUP_DIR/vkBasalt-reloaded.conf.bak" "$CONFIG_PATH"
        echo "✓ Restored config"
    elif [ -f "$BACKUP_DIR/config_did_not_exist" ]; then
        rm -f "$CONFIG_PATH"
        echo "✓ Removed config (did not exist before)"
    fi
    if [ -f "$BACKUP_DIR/vkBasalt.json.bak" ]; then
        cp "$BACKUP_DIR/vkBasalt.json.bak" "$MANIFEST_PATH"
        echo "✓ Restored manifest"
    elif [ -f "$BACKUP_DIR/manifest_did_not_exist" ]; then
        rm -f "$MANIFEST_PATH"
        echo "✓ Removed temporary manifest"
    fi
    rm -rf "$BACKUP_DIR" /tmp/pgo-local
    echo "✓ Cleanup complete"
}
trap cleanup EXIT

if [ -f "$CONFIG_PATH" ]; then
    cp "$CONFIG_PATH" "$BACKUP_DIR/vkBasalt-reloaded.conf.bak"
else
    touch "$BACKUP_DIR/config_did_not_exist"
fi
if [ -f "$MANIFEST_PATH" ]; then
    cp "$MANIFEST_PATH" "$BACKUP_DIR/vkBasalt.json.bak"
else
    touch "$BACKUP_DIR/manifest_did_not_exist"
fi

# Phase 0: Detect HDR display and prepare window placement
echo "Phase 0: Detecting HDR display..."
HDR_OUTPUT=""
HDR_X=0
HDR_Y=0

if command -v kscreen-doctor &>/dev/null; then
    current_output=""
    current_x=0
    current_y=0
    is_hdr=false
    while IFS= read -r line; do
        if [[ "$line" =~ ^Output:\ [0-9]+\ \"(.+)\"$ ]] || [[ "$line" =~ ^Output:\ [0-9]+\ (.+)$ ]]; then
            if [ "$is_hdr" = true ] && [ -n "$current_output" ]; then
                HDR_OUTPUT="$current_output"
                HDR_X="$current_x"
                HDR_Y="$current_y"
                break
            fi
            current_output="${BASH_REMATCH[1]}"
            is_hdr=false
        elif [[ "$line" =~ Position:\ ([0-9]+),([0-9]+) ]]; then
            current_x="${BASH_REMATCH[1]}"
            current_y="${BASH_REMATCH[2]}"
        elif [[ "$line" == *"HDR: enabled"* ]]; then
            is_hdr=true
        fi
    done < <(kscreen-doctor -o outputs 2>/dev/null)
    if [ "$is_hdr" = true ] && [ -n "$current_output" ] && [ -z "$HDR_OUTPUT" ]; then
        HDR_OUTPUT="$current_output"
        HDR_X="$current_x"
        HDR_Y="$current_y"
    fi
fi

if [ -n "$HDR_OUTPUT" ]; then
    echo "✓ Detected HDR output: $HDR_OUTPUT at position ${HDR_X},${HDR_Y}"
    if ! command -v xdotool &>/dev/null; then
        echo "  WARNING: xdotool not found. Install it (sudo pacman -S xdotool) to automatically move vkcube to the HDR monitor."
    fi
else
    echo "WARNING: Could not detect HDR output via kscreen-doctor. vkcube may open on the SDR monitor."
fi
echo ""

# Phase 0.5: HDR enforcement for PGO training
echo "HDR Enforcement for PGO Training:"
echo "  When enabled, ALL profiling configs will be forced through the HDR swapchain"
echo "  mutation path, exercising the AutoHdrAnalyzer compute shaders and the"
echo "  NitCalibration fragment shader even when autoHdr=false in the config."
echo "  This ensures the HDR hot paths are captured in the PGO profile data."
echo ""
read -p "Force HDR pathways during profiling? [y/N]: " hdr_force_confirm
FORCE_HDR_PGO=false
case "$hdr_force_confirm" in
    y|Y|yes|Yes|YES)
        FORCE_HDR_PGO=true
        export VKBASALT_FORCE_HDR_PGO=1
        echo "✓ HDR enforcement ENABLED (VKBASALT_FORCE_HDR_PGO=1)"
        ;;
    *)
        echo "  HDR enforcement disabled. Only configs with autoHdr=true will exercise HDR pathways."
        ;;
esac
echo ""

# Phase 1: Build instrumented binary
echo "Phase 1: Building instrumented binary..."
meson setup builddir-gen \
  --prefix=/usr \
  --buildtype=release \
  -Db_lto=true \
  -Db_lto_mode=thin \
  -Doptimization=2 \
  -Dpgo=generate \
  -Dpgo_dir=$(pwd)/pgo-data \
  -Dc_args='-march=native' \
  -Dcpp_args='-march=native'
meson compile -C builddir-gen
echo "✓ Instrumented build complete"
echo ""

# Phase 2: Install layer temporarily for profiling
echo "Phase 2: Installing layer for profiling..."
LAYER_SO_PATH="$(pwd)/builddir-gen/src/libvkbasalt.so"
mkdir -p ~/.local/share/vulkan/implicit_layer.d

cat > "$MANIFEST_PATH" << MANIFEST
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_VKBASALT_post_processing",
        "type": "GLOBAL",
        "library_path": "$LAYER_SO_PATH",
        "api_version": "1.3.223",
        "implementation_version": "1",
        "description": "a post processing layer",
        "functions": {
            "vkGetInstanceProcAddr": "vkBasalt_GetInstanceProcAddr",
            "vkGetDeviceProcAddr": "vkBasalt_GetDeviceProcAddr"
        },
        "enable_environment": {
            "ENABLE_VKBASALT_RELOADED": "1"
        },
        "disable_environment": {
            "DISABLE_VKBASALT_RELOADED": "1"
        }
    }
}
MANIFEST

export ENABLE_VKBASALT_RELOADED=1
export LLVM_PROFILE_FILE="$(pwd)/pgo-data/vkbasalt-%m-%p.profraw"
export VK_LOADER_LAYERS_DISABLE="*LSFG*,*lsfg*,*frame_gen*"
echo "✓ Layer installed (temporary)"
echo "✓ Disabled conflicting layers (lsfg-vk) via VK_LOADER_LAYERS_DISABLE"
echo ""

# Phase 3: Compile exit handler
echo "Phase 3: Compiling signal handler..."
mkdir -p /tmp/pgo-local
cat > /tmp/pgo-local/exit_handler.c << 'EOF'
#include <signal.h>
#include <stdlib.h>
static void on_signal(int s) { (void) s; exit(0); }
__attribute__((constructor)) static void init(void) { signal(SIGTERM, on_signal); signal(SIGINT, on_signal); }
EOF
clang -shared -fPIC -o /tmp/pgo-local/libexit_handler.so /tmp/pgo-local/exit_handler.c
echo "✓ Exit handler compiled"
echo ""

# Phase 4: Setup config
mkdir -p "$CONFIG_DIR"
mkdir -p "$CONFIG_DIR/games"

run_profile() {
    local duration=$1
    local log_file="${2:-/dev/null}"
    local exit_code=0
    # Force X11/XWayland so xdotool can manipulate the window under Wayland compositors
    local OLD_WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}"
    unset WAYLAND_DISPLAY
    LD_PRELOAD=/tmp/pgo-local/libexit_handler.so timeout --preserve-status --signal=TERM --kill-after=5 "$duration" vkcube > "$log_file" 2>&1 &
    local VK_PID=$!
    
    # Wait for window to map and move it to HDR monitor
    if [ -n "$HDR_OUTPUT" ] && command -v xdotool &>/dev/null; then
        for i in {1..20}; do
            WID=$(xdotool search --name "vkcube" 2>/dev/null | head -1)
            if [ -n "$WID" ]; then
                xdotool windowmove "$WID" "$HDR_X" "$HDR_Y" 2>/dev/null || true
                break
            fi
            sleep 0.1
        done
    fi
    
    wait "$VK_PID" 2>/dev/null || exit_code=$?
    
    # Restore Wayland display
    if [ -n "$OLD_WAYLAND_DISPLAY" ]; then
        export WAYLAND_DISPLAY="$OLD_WAYLAND_DISPLAY"
    fi
    
    echo "  run_profile(${duration}s) exited with code $exit_code"
    return 0
}

# Phase 5: Run profiling workloads
echo "Phase 5: Running profiling workloads on real GPU..."
if [ "$FORCE_HDR_PGO" = true ]; then
    echo "  [HDR ENFORCEMENT ACTIVE] All configs will be forced through HDR swapchain mutation."
fi
echo ""

echo "=== Config 1: CMAA2 Only (1-slice in-place compute) ==="
cat > "$CONFIG_PATH" << 'EOF'
effects=cmaa2
cmaa2QualityPreset=2
autoHdr=false
scopesEnabled=false
hdrCalibrationMode=off
enableOnLaunch=true
EOF
run_profile 30 /tmp/pgo-local/config1.log
if ! grep -q "vkBasalt info" /tmp/pgo-local/config1.log 2>/dev/null; then
    echo "ERROR: vkBasalt layer did not load. Check manifest and .so path."
    exit 1
fi
echo "✓ Layer load verified"

echo ""
echo "=== Config 2: CrystalClear Only (1-slice fragment) ==="
cat > "$CONFIG_PATH" << 'EOF'
effects=crystalclear
crystalclearPreset=devfav
autoHdr=false
scopesEnabled=false
hdrCalibrationMode=off
enableOnLaunch=true
EOF
run_profile 30

echo ""
echo "=== Config 3: Mega Chain + Scopes + HDR (2-slice ping-pong) ==="
cat > "$CONFIG_PATH" << 'EOF'
effects=fxaa:smaa:cmaa2:deband:dls:cas:clarity:clarityrcas:crystalclear
scopesEnabled=true
autoHdr=true
hdrCalibrationMode=manual
sdrWhitePointNits=203
hdrPeakNits=1000
casSharpness=0.5
clarityStrength=0.6
crystalclearPreset=devfav
debandRange=16.0
dlsSharpen=0.5
cmaa2QualityPreset=2
enableOnLaunch=true
EOF
run_profile 60

echo ""
echo "=== Config 4: Passthrough (no effects) ==="
cat > "$CONFIG_PATH" << 'EOF'
effects=
scopesEnabled=false
autoHdr=false
hdrCalibrationMode=off
enableOnLaunch=true
EOF
run_profile 15

echo ""
echo "=== Config 5: Depth Capture ==="
cat > "$CONFIG_PATH" << 'EOF'
effects=cas
depthCapture=on
autoHdr=false
scopesEnabled=false
hdrCalibrationMode=off
enableOnLaunch=true
EOF
run_profile 15

echo ""
echo "✓ Profiling complete"
echo ""

# Phase 6: Check profile files
echo "Phase 6: Checking profile data..."
if ! ls pgo-data/*.profraw 1>/dev/null 2>&1; then
    echo "ERROR: No .profraw files found in pgo-data/"
    exit 1
fi
echo "Found $(ls -1 pgo-data/*.profraw | wc -l) profile files"
ls -lh pgo-data/*.profraw
echo ""

# Phase 7: Merge profiles
echo "Phase 7: Merging profiles..."
llvm-profdata merge -output=pgo-data/default.profdata pgo-data/*.profraw
echo "✓ Merged profile: $(stat -c%s pgo-data/default.profdata) bytes"
echo ""

# Phase 8: Build optimized binary
echo "Phase 8: Building optimized binary..."
meson setup builddir-pgo \
  --prefix=/usr \
  --buildtype=release \
  -Db_lto=true \
  -Db_lto_mode=thin \
  -Doptimization=2 \
  -Dpgo=use \
  -Dpgo_dir=$(pwd)/pgo-data \
  -Dc_args='-march=native' \
  -Dcpp_args='-march=native'
meson compile -C builddir-pgo
echo "✓ Optimized build complete"
echo ""

# Phase 9: Compare sizes
echo "Phase 9: Binary size comparison"
echo "Instrumented: $(stat -c%s builddir-gen/src/libvkbasalt.so) bytes"
echo "PGO optimized: $(stat -c%s builddir-pgo/src/libvkbasalt.so) bytes"
if [ "$FORCE_HDR_PGO" = true ]; then
    echo "Profiled with: HDR enforcement ACTIVE (VKBASALT_FORCE_HDR_PGO=1)"
else
    echo "Profiled with: HDR enforcement disabled"
fi
echo ""
echo "--- Instrumented ---"
llvm-size builddir-gen/src/libvkbasalt.so
echo "--- PGO Optimized ---"
llvm-size builddir-pgo/src/libvkbasalt.so
echo ""

# Phase 10: Remove temporary profiling manifest before system install
rm -f "$MANIFEST_PATH"
touch "$BACKUP_DIR/manifest_did_not_exist"

# Phase 11: Offer system-wide install
echo "========================================="
echo "    PGO Build Complete!"
echo "========================================="
echo ""
read -p "Install PGO-optimized vkBasalt system-wide? [y/N]: " confirm

case "$confirm" in
    y|Y|yes|Yes|YES)
        read -p "Strip debug symbols? [Y/n]: " strip_confirm
        INSTALL_ARGS="-C builddir-pgo"
        if [[ ! "$strip_confirm" =~ ^[Nn] ]]; then
            INSTALL_ARGS="$INSTALL_ARGS --strip"
            echo "Installation will strip debug symbols."
        fi

        echo "Running system installation..."
        sudo meson install $INSTALL_ARGS
        sudo ldconfig

        # Remove backup so cleanup doesn't restore old manifest over system install
        rm -f "$BACKUP_DIR/vkBasalt.json.bak"
        touch "$BACKUP_DIR/manifest_did_not_exist"

        JSON_PATH="/usr/share/vulkan/implicit_layer.d/vkBasalt.json"
        if [ -f "$JSON_PATH" ]; then
            sudo sed -i 's|"library_path":.*|"library_path": "/usr/lib/libvkbasalt.so",|g' "$JSON_PATH"
            echo "✓ Patched Vulkan manifest"
        fi

        INSTALLED_SO="/usr/lib/libvkbasalt.so"
        if [ -f "$INSTALLED_SO" ]; then
            echo ""
            echo "Installed: $INSTALLED_SO ($(du -h "$INSTALLED_SO" | cut -f1))"
            if file "$INSTALLED_SO" | grep -q 'stripped'; then
                echo "Status: symbols stripped ✓"
            fi
        fi

        echo ""
        echo "Restart Steam fully (tray → Exit) so Proton re-reads the manifest."
        ;;
    *)
        echo "Skipping installation."
        echo "Install later: sudo meson install -C builddir-pgo --strip"
        ;;
esac
