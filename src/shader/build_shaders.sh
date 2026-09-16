#!/bin/bash
set -euo pipefail
export LC_ALL=C

GLSLANG="$1"
SPIRV_OPT="$2"
TARGET_ENV="$3"
ZSTD="$4"
OUTDIR="$5"
shift 5

GLSL_FILES=("$@")

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

SPV_FILES=()

GREEN=$'\033[0;32m'
BLUE=$'\033[0;34m'
NC=$'\033[0m'
BOLD=$'\033[1m'

format_bytes() {
    awk -v b="$1" 'BEGIN {
        if (b >= 1048576) printf "%.2f MB", b / 1048576;
        else if (b >= 1024) printf "%.2f KB", b / 1024;
        else printf "%d B", b;
    }'
}

get_color() {
    local tier
    tier=$(awk -v r="$1" 'BEGIN {
        if (r <= 0.05) print 5;
        else if (r <= 0.15) print 4;
        else if (r <= 0.25) print 3;
        else if (r <= 0.35) print 2;
        else print 1;
    }')
    
    case $tier in
        5) printf "\033[38;2;0;255;255m" ;;
        4) printf "\033[38;2;71;191;89m" ;;
        3) printf "\033[38;2;255;215;0m" ;;
        2) printf "\033[38;2;176;38;255m" ;;
        1) printf "\033[38;2;255;0;255m" ;;
    esac
}

create_bar() {
    local ratio=$1
    local color=$2
    local nc=$3
    local width=20
    local filled
    filled=$(awk -v r="$ratio" -v w="$width" 'BEGIN { printf "%d", (1 - r) * w }')
    local empty=$((width - filled))

    local bar_filled=""
    local bar_empty=""
    local i
    for ((i = 0; i < filled; i++)); do bar_filled+="#"; done
    for ((i = 0; i < empty; i++)); do bar_empty+="."; done
    
    printf "[%s%s%s%s]" "$color" "$bar_filled" "$nc" "$bar_empty"
}

# - Step 1: Compile and optimize all GLSL to SPIR-V -
for INPUT in "${GLSL_FILES[@]}"; do
    BASENAME=$(basename "$INPUT" .glsl)
    TEMP_SPV="$TEMP_DIR/${BASENAME}.spv"
    OPT_SPV="$TEMP_DIR/${BASENAME}.opt.spv"

    "$GLSLANG" --target-env "$TARGET_ENV" --lto --nan-clamp -g0 -I"$(dirname "$INPUT")" "$INPUT" -o "$TEMP_SPV" >/dev/null 2>&1

    "$SPIRV_OPT" \
        --target-env="$TARGET_ENV" \
        --preserve-spec-constants --preserve-bindings --preserve-interface \
        -O --strength-reduction --cfg-cleanup --eliminate-dead-members \
        --eliminate-dead-const --eliminate-dead-variables --eliminate-dead-input-components \
        --unify-const --remove-duplicates --trim-capabilities \
        --fold-spec-const-op-composite --strip-debug --strip-nonsemantic --compact-ids \
        "$TEMP_SPV" -o "$OPT_SPV" >/dev/null 2>&1

    SPV_FILES+=("$OPT_SPV")
done

# - Step 2: Train dictionary -
DICT_FILE="$TEMP_DIR/shader.zdict"
echo -e "${BLUE}Training zstd dictionary from ${#SPV_FILES[@]} SPIR-V files...${NC}"
"$ZSTD" --train --train-cover --maxdict=32768 --dictID=1 -o "$DICT_FILE" "${SPV_FILES[@]}" >/dev/null 2>&1
DICT_SIZE=$(stat -c%s "$DICT_FILE")
echo -e "${GREEN}Dictionary trained ($(format_bytes "$DICT_SIZE"))${NC}"

# - Step 3: Emit dictionary header -
DICT_H="$OUTDIR/shader_dict.h"
{
    echo "#pragma once"
    echo "#include <cstdint>"
    echo "#include <cstddef>"
    echo ""
    echo "static const uint8_t vkbasalt_shader_dict[] = {"
    od -A n -t x1 -v "$DICT_FILE" \
        | sed 's/^ *//;s/ *$//' \
        | awk '{for(i=1;i<=NF;i++) printf "0x%s,", $i; print ""}' \
        | sed 's/^/    /'
    echo "};"
    echo "static const size_t vkbasalt_shader_dict_size = sizeof(vkbasalt_shader_dict);"
} > "$DICT_H"

# - Step 4: Compress each SPIR-V and generate headers -
COL_SHADER=35
COL_SIZE=10
COL_RATIO=7
COL_SAVED=9
COL_BAR=22
LINE_WIDTH=$((1 + COL_SHADER + 3 + COL_SIZE + 3 + COL_SIZE + 3 + COL_RATIO + 3 + COL_SAVED + 3 + COL_BAR + 3))
RULE=$(printf '%*s' "$LINE_WIDTH" '' | tr ' ' '-')

echo ""
echo -e "${BOLD}${BLUE}Shader Compression Report${NC}"
echo -e "${BLUE}${RULE}${NC}"
printf "${BOLD} %-${COL_SHADER}s | %${COL_SIZE}s | %${COL_SIZE}s | %${COL_RATIO}s | %${COL_SAVED}s | %-${COL_BAR}s${NC}\n" \
    "Shader" "Original" "Compressed" "Ratio" "Saved" "Compression"
echo -e "${BLUE}${RULE}${NC}"

TOTAL_ORIGINAL=0
TOTAL_COMPRESSED=0
SHADER_COUNT=0

for OPT_SPV in "${SPV_FILES[@]}"; do
    BASENAME=$(basename "$OPT_SPV" .opt.spv)
    ZST_FILE="$TEMP_DIR/${BASENAME}.zst"
    OUTPUT="$OUTDIR/${BASENAME}.h"

    "$ZSTD" -19 --single-thread --no-check --no-dictID -f -D "$DICT_FILE" "$OPT_SPV" -o "$ZST_FILE" >/dev/null 2>&1
    "$ZSTD" -t -D "$DICT_FILE" "$ZST_FILE" >/dev/null 2>&1

    ORIGINAL_SIZE=$(stat -c%s "$OPT_SPV")
    COMPRESSED_SIZE=$(stat -c%s "$ZST_FILE")

    read RATIO PERCENT SAVED_PERCENT < <(awk -v c="$COMPRESSED_SIZE" -v o="$ORIGINAL_SIZE" 'BEGIN {
        r = c / o; printf "%.4f %.1f %.1f\n", r, r * 100, (1 - r) * 100
    }')

    COLOR=$(get_color "$RATIO")
    BAR=$(create_bar "$RATIO" "$COLOR" "$NC")

    printf " %-${COL_SHADER}s | %${COL_SIZE}s | %${COL_SIZE}s | %s%${COL_RATIO}s%s | %${COL_SAVED}s | %s\n" \
        "${BASENAME}.glsl" \
        "$(format_bytes "$ORIGINAL_SIZE")" \
        "$(format_bytes "$COMPRESSED_SIZE")" \
        "$COLOR" "${PERCENT}%" "$NC" \
        "${SAVED_PERCENT}%" \
        "$BAR"

    TOTAL_ORIGINAL=$((TOTAL_ORIGINAL + ORIGINAL_SIZE))
    TOTAL_COMPRESSED=$((TOTAL_COMPRESSED + COMPRESSED_SIZE))
    SHADER_COUNT=$((SHADER_COUNT + 1))

    VARNAME=$(echo "$BASENAME" | sed 's/[.-]/_/g')
    {
        echo "#pragma once"
        echo "#include <cstdint>"
        echo "#include <cstddef>"
        echo ""
        echo "static const size_t ${VARNAME}_spirv_size = ${ORIGINAL_SIZE};"
        echo ""
        echo "static const uint8_t ${VARNAME}_zst[] = {"
        od -A n -t x1 -v "$ZST_FILE" \
            | sed 's/^ *//;s/ *$//' \
            | awk '{for(i=1;i<=NF;i++) printf "0x%s,", $i; print ""}' \
            | sed 's/^/    /'
        echo "};"
        echo "static const size_t ${VARNAME}_zst_size = sizeof(${VARNAME}_zst);"
    } > "$OUTPUT"
done

echo -e "${BLUE}${RULE}${NC}"

TOTAL_SAVED=$((TOTAL_ORIGINAL - TOTAL_COMPRESSED))
read TOTAL_RATIO TOTAL_PERCENT TOTAL_SAVED_PERCENT < <(awk -v c="$TOTAL_COMPRESSED" -v o="$TOTAL_ORIGINAL" 'BEGIN {
    r = c / o; printf "%.4f %.1f %.1f\n", r, r * 100, (1 - r) * 100
}')
TOTAL_COLOR=$(get_color "$TOTAL_RATIO")

echo -e "${BOLD}Summary:${NC}"
echo -e "  Shaders processed:   ${BOLD}$SHADER_COUNT${NC}"
echo -e "  Total original size: $(format_bytes "$TOTAL_ORIGINAL")"
echo -e "  Total compressed:    $(format_bytes "$TOTAL_COMPRESSED")"
echo -e "  Total saved:         ${GREEN}$(format_bytes "$TOTAL_SAVED") (${TOTAL_SAVED_PERCENT}%)${NC}"
echo -e "  Overall ratio:       ${TOTAL_COLOR}${TOTAL_PERCENT}%${NC}"
echo -e "  Dictionary size:     $(format_bytes "$DICT_SIZE")"
echo ""
echo -e "${GREEN}All shaders compressed and headers generated${NC}"
