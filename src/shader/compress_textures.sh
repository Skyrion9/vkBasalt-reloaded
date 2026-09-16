#!/bin/bash
set -euo pipefail
export LC_ALL=C

ZSTD="$1"
OUTDIR="$2"
AREA_H="$3"
SEARCH_H="$4"

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

extract_to_bin() {
    local header="$1" array_name="$2" out_bin="$3"
    awk -v arr="$array_name" '
    BEGIN { for(i=0;i<16;i++) { h[sprintf("%x",i)]=i; h[sprintf("%X",i)]=i } in_arr=0 }
    $0 ~ arr "[ \t]*\\[[^]]*\\][ \t]*=" { in_arr=1; next }
    in_arr {
        is_last = index($0, ";") > 0
        sub(/[ \t]*}[ \t]*;.*/, "", $0)
        while (match($0, /0x[0-9a-fA-F]+/)) {
            hex = substr($0, RSTART+2, RLENGTH-2)
            val = 0
            for(i=1;i<=length(hex);i++) val = val*16 + h[substr(hex,i,1)]
            printf "%c", val
            $0 = substr($0, RSTART+RLENGTH)
        }
        if (is_last) in_arr=0
    }' "$header" > "$out_bin"
}

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

echo -e "${BLUE}Extracting texture data from C headers...${NC}"
extract_to_bin "$AREA_H" "areaTexBytes" "$TEMP_DIR/area.bin"
extract_to_bin "$SEARCH_H" "searchTexBytes" "$TEMP_DIR/search.bin"

"$ZSTD" -19 --single-thread --no-check -f "$TEMP_DIR/area.bin" -o "$TEMP_DIR/area.zst" >/dev/null 2>&1
"$ZSTD" -t "$TEMP_DIR/area.zst" >/dev/null 2>&1

"$ZSTD" -19 --single-thread --no-check -f "$TEMP_DIR/search.bin" -o "$TEMP_DIR/search.zst" >/dev/null 2>&1
"$ZSTD" -t "$TEMP_DIR/search.zst" >/dev/null 2>&1

AREA_SIZE=$(stat -c%s "$TEMP_DIR/area.bin")
AREA_ZST_SIZE=$(stat -c%s "$TEMP_DIR/area.zst")
SEARCH_SIZE=$(stat -c%s "$TEMP_DIR/search.bin")
SEARCH_ZST_SIZE=$(stat -c%s "$TEMP_DIR/search.zst")

COL_TEX=15
COL_SIZE=10
COL_RATIO=7
COL_SAVED=7
COL_BAR=22
LINE_WIDTH=$((1 + COL_TEX + 3 + COL_SIZE + 3 + COL_SIZE + 3 + COL_RATIO + 3 + COL_SAVED + 3 + COL_BAR + 3))
RULE=$(printf '%*s' "$LINE_WIDTH" '' | tr ' ' '-')

echo ""
echo -e "${BOLD}${BLUE}Texture Compression Report${NC}"
echo -e "${BLUE}${RULE}${NC}"
printf "${BOLD} %-${COL_TEX}s | %${COL_SIZE}s | %${COL_SIZE}s | %${COL_RATIO}s | %${COL_SAVED}s | %-${COL_BAR}s${NC}\n" \
    "Texture" "Original" "Compressed" "Ratio" "Saved" "Compression"
echo -e "${BLUE}${RULE}${NC}"

print_row() {
    local name="$1" orig="$2" comp="$3"
    local ratio percent saved_percent
    
    read ratio percent saved_percent < <(awk -v c="$comp" -v o="$orig" 'BEGIN {
        r = c / o; printf "%.4f %.1f %.1f\n", r, r * 100, (1 - r) * 100
    }')
    
    local color=$(get_color "$ratio")
    local bar=$(create_bar "$ratio" "$color" "$NC")
    
    printf " %-${COL_TEX}s | %${COL_SIZE}s | %${COL_SIZE}s | %s%${COL_RATIO}s%s | %${COL_SAVED}s | %s\n" \
        "$name" "$(format_bytes "$orig")" "$(format_bytes "$comp")" \
        "$color" "${percent}%" "$NC" \
        "${saved_percent}%" \
        "$bar"
}

print_row "AreaTex.h" "$AREA_SIZE" "$AREA_ZST_SIZE"
print_row "SearchTex.h" "$SEARCH_SIZE" "$SEARCH_ZST_SIZE"

echo -e "${BLUE}${RULE}${NC}"

TOTAL_ORIG=$((AREA_SIZE + SEARCH_SIZE))
TOTAL_COMP=$((AREA_ZST_SIZE + SEARCH_ZST_SIZE))
TOTAL_SAVED=$((TOTAL_ORIG - TOTAL_COMP))

read TOTAL_RATIO TOTAL_PERCENT TOTAL_SAVED_PERCENT < <(awk -v c="$TOTAL_COMP" -v o="$TOTAL_ORIG" 'BEGIN {
    r = c / o; printf "%.4f %.1f %.1f\n", r, r * 100, (1 - r) * 100
}')
TOTAL_COLOR=$(get_color "$TOTAL_RATIO")

echo -e "${BOLD}Summary:${NC}"
echo -e "  Total original:   $(format_bytes "$TOTAL_ORIG")"
echo -e "  Total compressed: $(format_bytes "$TOTAL_COMP")"
echo -e "  Total saved:      ${GREEN}$(format_bytes "$TOTAL_SAVED") (${TOTAL_SAVED_PERCENT}%)${NC}"
echo -e "  Overall ratio:    ${TOTAL_COLOR}${TOTAL_PERCENT}%${NC}"
echo ""

OUT_CPP="$OUTDIR/texture_data.cpp"
{
    echo "#include \"texture_data.hpp\""
    echo ""
    echo "namespace vkBasalt {"
    echo ""
    echo "const size_t areaTex_size = $AREA_SIZE;"
    echo "const uint8_t areaTex_zst[] = {"
    od -A n -t x1 -v "$TEMP_DIR/area.zst" | sed 's/^ *//;s/ *$//' | awk '{for(i=1;i<=NF;i++) printf "0x%s,", $i; print ""}' | sed 's/^/    /'
    echo "};"
    echo "const size_t areaTex_zst_size = sizeof(areaTex_zst);"
    echo ""
    echo "const size_t searchTex_size = $SEARCH_SIZE;"
    echo "const uint8_t searchTex_zst[] = {"
    od -A n -t x1 -v "$TEMP_DIR/search.zst" | sed 's/^ *//;s/ *$//' | awk '{for(i=1;i<=NF;i++) printf "0x%s,", $i; print ""}' | sed 's/^/    /'
    echo "};"
    echo "const size_t searchTex_zst_size = sizeof(searchTex_zst);"
    echo ""
    echo "} // namespace vkBasalt"
} > "$OUT_CPP"

echo -e "${GREEN}Generated $OUT_CPP${NC}"
