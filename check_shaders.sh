#!/bin/bash
# Runs glslangValidator on every shader in src/shader and reports all errors.

GLSLANG="${GLSLANG:-glslang}"
TARGET_ENV="vulkan1.4"
SHADER_DIR="src/shader"
INCLUDE_DIR="$SHADER_DIR"

if ! command -v "$GLSLANG" &> /dev/null; then
    echo "ERROR: glslang not found. Install it or set GLSLANG env var."
    exit 1
fi

failed=0
total=0

for shader in "$SHADER_DIR"/*.glsl; do
    [ -f "$shader" ] || continue

    # Skip include only headers (no stage suffix)
    case "$shader" in
        *.comp.glsl|*.frag.glsl|*.vert.glsl|*.geom.glsl|*.tesc.glsl|*.tese.glsl) ;;
        *) echo "=== $shader === (skipped: include header)"; echo ""; continue ;;
    esac

    total=$((total + 1))
    echo "=== $shader ==="

    if ! "$GLSLANG" -V --target-env "$TARGET_ENV" -I"$INCLUDE_DIR" "$shader" -o /dev/null; then
        failed=$((failed + 1))
    fi
    echo ""
done

echo "==================================="
echo "Checked: $total shaders"
echo "Failed:  $failed shaders"
[ "$failed" -eq 0 ] && echo "All shaders compiled successfully!" || exit 1
