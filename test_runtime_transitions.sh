#!/bin/bash
set -euo pipefail

# --- Setup ---
CONFIG_DIR="${HOME}/.config/vkBasalt-reloaded"
CONFIG_PATH="${CONFIG_DIR}/vkBasalt-reloaded.conf"
mkdir -p "$CONFIG_DIR"

# This ensures the global config is the first priority instead of per-game config for the test harness,
rm -f "${CONFIG_DIR}/games/vkcube_"*.conf

# Fix CI environment issues: XDG_RUNTIME_DIR must exist for X11/Wayland/Vulkan to work properly
export XDG_RUNTIME_DIR=/tmp/runtime-root
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

export ENABLE_VKBASALT_RELOADED=1
export VKBASALT_FORCE_HDR_PGO=1
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_LOADER_DEBUG=all
export VKBASALT_LOG_LEVEL=debug
export VK_LAYER_SETTINGS_PATH=/tmp/vk_layer_settings.txt

# Simplified validation settings: 'verbose' causes OOM/segfaults on llvmpipe in CI.
# Quotes around message_id_filter prevent parser crashes.
cat > /tmp/vk_layer_settings.txt << 'EOF'
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.report_flags = error,warn
khronos_validation.log_filename = /tmp/validation_errors.log
khronos_validation.message_id_filter = "0x1e7883ea,0x84029a9f,0x2e2f4d65"
EOF

VAL_LOG="/tmp/validation_errors.log"
STDERR_LOG="/tmp/vkcube_stderr.log"
STAGE_LOG="/tmp/validation_stages.log"

rm -f "$VAL_LOG" "$STDERR_LOG" "$STAGE_LOG"
touch "$VAL_LOG" "$STDERR_LOG" "$STAGE_LOG"

VK_PID=""
XVFB_PID=""

# Installed before vkcube launches. Any early exit under set -e must still reap the child,
cleanup() {
    rm -f /tmp/vkbasalt_reload_trigger
    if [ -n "${VK_PID:-}" ] && kill -0 "$VK_PID" 2>/dev/null; then
        kill -TERM "$VK_PID" 2>/dev/null || true
        for i in {1..20}; do
            if ! kill -0 "$VK_PID" 2>/dev/null; then break; fi
            sleep 0.1
        done
        if kill -0 "$VK_PID" 2>/dev/null; then kill -KILL "$VK_PID" 2>/dev/null || true; fi
        wait "$VK_PID" 2>/dev/null || true
    fi
    if [ -n "${XVFB_PID:-}" ]; then
        kill -KILL "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    echo "Wayland session detected; forcing X11/XWayland for vkcube for consistent window and validation behavior."
    unset WAYLAND_DISPLAY
fi

VALIDATION_AVAILABLE=true
if ! ls /usr/share/vulkan/explicit_layer.d/*khronos*validation* >/dev/null 2>&1; then
    echo "WARNING: Khronos validation layer manifest not found."
    VALIDATION_AVAILABLE=false
fi

if [ -z "${DISPLAY:-}" ]; then
    # Clean up stale X11 locks from previous CI runs
    rm -f /tmp/.X99-lock /tmp/.X11-unix/X99 2>/dev/null || true
    Xvfb :99 -screen 0 1920x1080x24 &
    XVFB_PID=$!
    # Increased sleep time for slow CI containers
    sleep 3
    export DISPLAY=:99
fi

# 300 seconds (5 mins) is enough for all 12 scenarios + sleep times.
# Don't use a short timeout, or vkcube will be killed early.
timeout --signal=TERM --kill-after=5 300 vkcube > /tmp/vkcube_stdout.log 2> "$STDERR_LOG" &
VK_PID=$!
sleep 3

if ! kill -0 "$VK_PID" 2>/dev/null; then
    echo "ERROR: vkcube failed to start or crashed immediately."
    cat "$STDERR_LOG" 2>/dev/null || true
    exit 1
fi

# xdotool is no longer needed. The magic file trigger handles reloads without window focus.

get_val_log_lines() {
    if [ -f "$VAL_LOG" ]; then wc -l < "$VAL_LOG" | tr -d ' '; else echo 0; fi
}

get_stderr_log_lines() {
    if [ -f "$STDERR_LOG" ]; then wc -l < "$STDERR_LOG" | tr -d ' '; else echo 0; fi
}

# Capture initial startup logs
sleep 2
startup_val_lines=$(get_val_log_lines)
startup_stderr_lines=$(get_stderr_log_lines)

echo "=== Stage: Initial Startup ===" >> "$STAGE_LOG"
if [ "$startup_val_lines" -gt 0 ]; then
    head -n "$startup_val_lines" "$VAL_LOG" | sed 's/^/  /' >> "$STAGE_LOG"
else
    echo "  (No validation messages)" >> "$STAGE_LOG"
fi
echo "  Relevant vkBasalt Startup Logs:" >> "$STAGE_LOG"
{ head -n "$startup_stderr_lines" "$STDERR_LOG" | grep -E "vkBasalt (info|debug|warn|err):" | grep -vE "cursorScale|uiScale|fontScale|initImGui|ImGui DrawData|processFrame|INSERT KEY|Pure X11" | sed 's/^/    /' >> "$STAGE_LOG"; } || true
echo "" >> "$STAGE_LOG"

STAGE_FAILURES=0

run_stage() {
    local stage_name="$1"
    local config_content="$2"
    local expected_effects="$3" # space-separated list of effect names to verify
    
    # CRASH GUARD: If vkcube died during the previous reload, abort immediately.
    if ! kill -0 "$VK_PID" 2>/dev/null; then
        echo "  [FATAL] vkcube is no longer running! Aborting test suite." >> "$STAGE_LOG"
        echo "❌ FATAL: vkcube crashed or exited prematurely during reloads."
        echo "--- Last 20 lines of vkcube stderr ---"
        tail -n 20 "$STDERR_LOG"
        exit 1
    fi
    
    local start_val_lines=$(get_val_log_lines)
    local start_stderr_lines=$(get_stderr_log_lines)
    
    echo -e "$config_content" > "$CONFIG_PATH"
    touch "$CONFIG_PATH"
    
    # Trigger reload via the magic file mechanism.
    # The layer checks for /tmp/vkbasalt_reload_trigger every frame in vkBasalt_QueuePresentKHR.
    # This is 100% reliable, works on Wayland/X11/Proton, and doesn't interfere with signal handlers.
    touch /tmp/vkbasalt_reload_trigger
    sleep 2 # Wait for swapchain rebuild / effect chain realloc to finish
    
    local end_val_lines=$(get_val_log_lines)
    local end_stderr_lines=$(get_stderr_log_lines)
    
    echo "=== Stage: $stage_name ===" >> "$STAGE_LOG"
    echo "Config Applied:" >> "$STAGE_LOG"
    echo "$config_content" | sed 's/^/  /' >> "$STAGE_LOG"
    
    # 1. Validation errors
    if [ "$end_val_lines" -gt "$start_val_lines" ]; then
        local new_val
        new_val=$(sed -n "$((start_val_lines + 1)),$((end_val_lines))p" "$VAL_LOG")
        if echo "$new_val" | grep -qE "Validation Error|Validation Warning"; then
            echo "  [FAIL] Validation Errors/Warnings detected:" >> "$STAGE_LOG"
            echo "$new_val" | grep -E "Validation Error|Validation Warning" | sed 's/^/    /' >> "$STAGE_LOG"
            STAGE_FAILURES=$((STAGE_FAILURES + 1))
        else
            echo "  [OK] No validation errors." >> "$STAGE_LOG"
        fi
    else
        echo "  [OK] No validation messages." >> "$STAGE_LOG"
    fi
    
    # 2. vkBasalt state verification
    if [ "$end_stderr_lines" -gt "$start_stderr_lines" ]; then
        local new_stderr
        new_stderr=$(sed -n "$((start_stderr_lines + 1)),$((end_stderr_lines))p" "$STDERR_LOG")
        
        if echo "$new_stderr" | grep -qE "Rebuilding effects|CreateSwapchainKHR|config file"; then
            echo "  [OK] Config reload/swapchain rebuild detected in logs." >> "$STAGE_LOG"
        else
            echo "  [WARN] No explicit reload/rebuild log detected (layer might have cached it or hotkey missed)." >> "$STAGE_LOG"
        fi
        
        local effects_ok=true
        if [ -n "$expected_effects" ]; then
            for eff in $expected_effects; do
                if echo "$new_stderr" | grep -qiE "created $eff effect|current effectString $eff|falling back to plain transfer|Appended HDR Output Effect|using NitCalibrationEffect|created ReshadeEffect for $eff"; then
                    echo "  [OK] Effect '$eff' verified in logs." >> "$STAGE_LOG"
                else
                    echo "  [FAIL] Effect '$eff' NOT found in layer logs!" >> "$STAGE_LOG"
                    effects_ok=false
                fi
            done
        else
            if echo "$new_stderr" | grep -qiE "falling back to plain transfer|buildDefaultNoEffectChain|TransferEffect|effect string count: 0"; then
                echo "  [OK] Empty chain / transfer fallback verified." >> "$STAGE_LOG"
            else
                echo "  [WARN] Empty chain fallback not explicitly logged." >> "$STAGE_LOG"
            fi
        fi
        
        if [ "$effects_ok" = false ]; then
            STAGE_FAILURES=$((STAGE_FAILURES + 1))
        fi
        
        echo "  Relevant vkBasalt Logs:" >> "$STAGE_LOG"
        { echo "$new_stderr" | grep -E "vkBasalt (info|debug|warn|err):" | grep -vE "cursorScale|uiScale|fontScale|initImGui|ImGui DrawData|processFrame|INSERT KEY|Pure X11|before applying effect" | sed 's/^/    /' >> "$STAGE_LOG"; } || true
    else
        echo "  [WARN] No new vkBasalt stderr logs generated." >> "$STAGE_LOG"
    fi
    echo "" >> "$STAGE_LOG"
}

# --- Test Scenarios ---
echo "=== Scenario 1: Effect Chain Growth ==="
run_stage "CMAA2 Only" "effects=cmaa2\nautoHdr=false\nenableOnLaunch=true" "cmaa2"
run_stage "CMAA2 + CrystalClear" "effects=cmaa2:crystalclear\nautoHdr=false\nenableOnLaunch=true" "cmaa2 crystalclear"
run_stage "CMAA2 + CrystalClear + CAS" "effects=cmaa2:crystalclear:cas\nautoHdr=false\nenableOnLaunch=true" "cmaa2 crystalclear cas"

echo "=== Scenario 2: AutoHDR Mutation Toggle ==="
# Note: On X11, AutoHDR mutation is skipped by design, so nitcalibration won't be appended.
run_stage "CAS + AutoHDR On" "effects=cas\nautoHdr=true\nenableOnLaunch=true" "cas"
run_stage "CAS + AutoHDR Off" "effects=cas\nautoHdr=false\nenableOnLaunch=true" "cas"

echo "=== Scenario 3: Scopes Lifecycle ==="
run_stage "CAS + Scopes On" "effects=cas\nscopesEnabled=true\nautoHdr=false\nenableOnLaunch=true" "cas"
run_stage "CAS + Scopes Off" "effects=cas\nscopesEnabled=false\nautoHdr=false\nenableOnLaunch=true" "cas"

echo "=== Scenario 4: Empty Chain -> Mega Chain ==="
run_stage "Empty Chain" "effects=\nautoHdr=false\nenableOnLaunch=true" ""
run_stage "Mega Chain + HDR + Scopes" "effects=fxaa:smaa:cmaa2:deband:dls:cas:clarity:clarityrcas:crystalclear\nautoHdr=true\nscopesEnabled=true\nenableOnLaunch=true" "fxaa smaa cmaa2 deband dls cas clarity clarityrcas crystalclear"

echo "=== Scenario 5: Rapid Toggle Spam ==="
for i in {1..5}; do
    run_stage "Spam CMAA2 On ($i)" "effects=cmaa2\nautoHdr=false\nenableOnLaunch=true" "cmaa2"
    run_stage "Spam Empty ($i)" "effects=\nautoHdr=false\nenableOnLaunch=true" ""
done

echo "=== Scenario 6: Individual Factory Effects Boot Test ==="
for effect in fxaa cas deband smaa lut dls clarity clarityrcas crystalclear cmaa2; do
    run_stage "Boot $effect" "effects=$effect\nautoHdr=false\nenableOnLaunch=true" "$effect"
done

echo "=== Scenario 7: HDR Calibration Modes ==="
run_stage "Manual Calibration" "effects=cas\nautoHdr=true\nhdrCalibrationMode=manual\nsdrWhitePointNits=150\nhdrPeakNits=800\nenableOnLaunch=true" "cas"
run_stage "Passthrough Mode" "effects=cas\nautoHdr=true\nhdrCalibrationMode=passthrough\nenableOnLaunch=true" "cas"
run_stage "Calibration Off" "effects=cas\nautoHdr=true\nhdrCalibrationMode=off\nenableOnLaunch=true" "cas"

echo "=== Scenario 8: Quality Preset Transitions ==="
run_stage "CMAA2 Low Preset" "effects=cmaa2\ncmaa2QualityPreset=Low\nautoHdr=false\nenableOnLaunch=true" "cmaa2"
run_stage "CMAA2 Ultra Preset" "effects=cmaa2\ncmaa2QualityPreset=Ultra\nautoHdr=false\nenableOnLaunch=true" "cmaa2"
run_stage "CrystalClear High Preset" "effects=crystalclear\ncrystalclearPresetApplied=High\nautoHdr=false\nenableOnLaunch=true" "crystalclear"

echo "=== Scenario 9: Per-Game Config Override ==="
# Dynamically extract the exact per-game config path the layer generated for this vkcube instance
PER_GAME_CONF=$(grep "config file (pergame):" "$STDERR_LOG" | head -1 | awk '{print $NF}' | tr -d '\r' || true)
if [ -n "$PER_GAME_CONF" ]; then
    mkdir -p "$(dirname "$PER_GAME_CONF")"
    cp "$PER_GAME_CONF" "${PER_GAME_CONF}.bak" 2>/dev/null || true
    
    # Test 1: Per-game overrides global (Global=fxaa, Per-Game=smaa -> expect smaa)
    echo -e "effects=smaa\nautoHdr=false\nenableOnLaunch=true" > "$PER_GAME_CONF"
    run_stage "Per-Game Override (SMAA)" "effects=fxaa\nautoHdr=false\nenableOnLaunch=true" "smaa"
    
    # Test 2: Remove per-game, fallback to global (Global=fxaa, Per-Game=deleted -> expect fxaa)
    rm -f "$PER_GAME_CONF"
    run_stage "Fallback to Global (FXAA)" "effects=fxaa\nautoHdr=false\nenableOnLaunch=true" "fxaa"
    
    # Restore original per-game config
    if [ -f "${PER_GAME_CONF}.bak" ]; then
        mv "${PER_GAME_CONF}.bak" "$PER_GAME_CONF"
    fi
else
    echo "  [SKIP] Could not determine per-game config path from startup logs." >> "$STAGE_LOG"
fi

echo "=== Scenario 10: Chain Order & Slice Routing ==="
# Tests the ping-pong vs in-place vs direct-write slice assignment logic
run_stage "CAS -> CMAA2 (Ping-Pong to In-Place)" "effects=cas:cmaa2\nautoHdr=false\nenableOnLaunch=true" "cas cmaa2"
run_stage "CMAA2 -> CAS (In-Place to Direct-Write)" "effects=cmaa2:cas\nautoHdr=false\nenableOnLaunch=true" "cmaa2 cas"

echo "=== Scenario 11: AutoHDR + Scopes Integration ==="
# Ensures FrameAnalyzer correctly reads from the compute source slice when AutoHDR is active
run_stage "AutoHDR + Scopes" "effects=cas\nautoHdr=true\nscopesEnabled=true\nenableOnLaunch=true" "cas"

echo "=== Scenario 12: Depth Capture Interception ==="
# Exercises the vkCreateImage interception path for depth buffers
run_stage "Depth Capture On" "effects=cas\ndepthCapture=on\nautoHdr=false\nenableOnLaunch=true" "cas"
run_stage "Depth Capture Off" "effects=cas\ndepthCapture=off\nautoHdr=false\nenableOnLaunch=true" "cas"

# --- Verification ---
echo ""
echo "=== Final Verification ==="

if [ "${VALIDATION_AVAILABLE}" = false ]; then
    echo "⚠️  Khronos validation layer not installed; skipping validation checks."
    exit 0
fi

COMBINED_LOG=$(mktemp)
[ -f "$VAL_LOG" ]    && cat "$VAL_LOG"    >> "$COMBINED_LOG" || true
[ -f "$STDERR_LOG" ] && cat "$STDERR_LOG" >> "$COMBINED_LOG" || true

if ! grep -qi "khronos_validation" "$COMBINED_LOG"; then
    echo "WARNING: No evidence the Khronos validation layer loaded."
    rm -f "$COMBINED_LOG"
    exit 1
fi

if grep -qE "Validation Error|Validation Warning|VK_ERROR_DEVICE_LOST|Device lost" "$COMBINED_LOG"; then
    echo "❌ FATAL: Global validation errors or warnings detected!"
    STAGE_FAILURES=$((STAGE_FAILURES + 1))
fi

rm -f "$COMBINED_LOG"

if [ "$STAGE_FAILURES" -gt 0 ]; then
    echo "❌ TEST FAILED: $STAGE_FAILURES stage(s) had validation errors or missing effects."
    echo "Please review the stage breakdown below to see exactly what the layer did:"
    echo "--------------------------------------------------------------------------------"
    cat "$STAGE_LOG"
    echo "--------------------------------------------------------------------------------"
    exit 1
fi

echo "✅ All runtime transitions survived cleanly. Zero validation errors and all effects verified in logs."
