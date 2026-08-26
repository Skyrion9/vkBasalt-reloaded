#!/bin/bash
# Compiles GLSL -> SPIR-V -> spirv-opt -> hex values (glslang -x compatible format).
# Outputs ONLY comma-separated uint32 hex values. shader_sources.hpp wraps
# these in the `static const uint32_t name[] = { ... };` array declaration.
set -euo pipefail

GLSLANG="$1"
SPIRV_OPT="$2"
TARGET_ENV="$3"
INPUT="$4"
OUTPUT="$5"

TEMP_SPV=$(mktemp /tmp/vkbasalt_shader_XXXXXX.spv)
OPT_SPV=$(mktemp /tmp/vkbasalt_shader_XXXXXX.spv)
trap 'rm -f "$TEMP_SPV" "$OPT_SPV"' EXIT

# 1. Compile GLSL -> SPIR-V binary for the resolved target.
#    --target-env <env> implies -V (do NOT add -V).
"$GLSLANG" --target-env "$TARGET_ENV" --lto --nan-clamp -g0 -I"$(dirname "$INPUT")" "$INPUT" -o "$TEMP_SPV"

# 2. Optimize SPIR-V for maximum GPU runtime performance.
#    spirv-opt uses --flag=value syntax for valued flags.
"$SPIRV_OPT" \
    -O \
    --strip-debug \
    --strip-nonsemantic \
    --preserve-bindings \
    --preserve-interface \
    --preserve-spec-constants \
    --skip-validation \
    --target-env="$TARGET_ENV" \
    "$TEMP_SPV" -o "$OPT_SPV"

# 3. Convert optimized SPIR-V -> comma-separated hex uint32 values (glslang -x format).
od -A n -t x4 -v "$OPT_SPV" | tr ' ' '\n' | sed '/^$/d' | sed 's/.*/0x&,/' > "$OUTPUT"
