#!/usr/bin/env bash
set -euo pipefail

# Lower build process priority (don't hog the system while compiling)
renice -n 19 -p $$ >/dev/null 2>&1 || true
ionice -c 3 -p $$ >/dev/null 2>&1 || true

cd "$(dirname "${BASH_SOURCE[0]}")"

export CC=clang
export CXX=clang++
export CC_LD=mold
export CXX_LD=mold

if command -v ccache &>/dev/null; then
    export CC="ccache clang"
    export CXX="ccache clang++"
    echo "✓ ccache detected and enabled."
fi

read -p "Optimize for this specific CPU (-march=native)? [Y/n]: " march_confirm
USE_MARCH=true
case "$march_confirm" in
    n|N|no|No|NO) USE_MARCH=false ;;
esac

MESON_ARGS=(
    --prefix=/usr
    --buildtype=release
    -Db_lto=true
    -Db_lto_mode=thin
    -Doptimization=2
)

if [ "$USE_MARCH" = true ]; then
    MESON_ARGS+=(-Dc_args='-march=native' -Dcpp_args='-march=native')
    echo "✓ Targeting native CPU architecture."
else
    echo "✓ Targeting generic x86_64."
fi

if [ -d build ]; then
    meson setup --reconfigure build "${MESON_ARGS[@]}"
else
    meson setup build "${MESON_ARGS[@]}"
fi

if meson compile -C build; then
    echo ""
    echo "========================================="
    echo "    Compilation Successful!"
    echo "========================================="
    echo ""
    read -p "Install vkBasalt system-wide right now? [y/N]: " confirm
    case "$confirm" in
        y|Y|yes|Yes|YES)
            read -p "Strip debug symbols? [Y/n]: " strip_confirm
            INSTALL_ARGS="-C build"
            if [[ ! "$strip_confirm" =~ ^[Nn] ]]; then
                INSTALL_ARGS="$INSTALL_ARGS --strip"
                echo "Installation will strip debug symbols."
            else
                echo "Installation will keep debug symbols."
            fi
            
            echo "Running system installation..."
            sudo meson install $INSTALL_ARGS
            sudo ldconfig
            
            JSON_PATH="/usr/share/vulkan/implicit_layer.d/VkLayer_Basalt_reloaded.json"
            INSTALLED_SO="/usr/lib/libVkLayer_Basalt_reloaded.so"
            OLD_JSON="/usr/share/vulkan/implicit_layer.d/vkBasalt.json"
            
            if [ -f "$OLD_JSON" ]; then
                sudo rm -f "$OLD_JSON"
                echo "✓ Removed old conflicting manifest: $OLD_JSON"
            fi
            
            if [ -f "$JSON_PATH" ]; then
                sudo sed -i 's|"library_path":.*|"library_path": "/usr/lib/libVkLayer_Basalt_reloaded.so",|g' "$JSON_PATH"
                echo "✓ Patched Vulkan manifest to point to /usr/lib/libVkLayer_Basalt_reloaded.so"
            else
                echo "✗ ERROR: Vulkan manifest not found at $JSON_PATH"
                exit 1
            fi
            
            # Verification checks
            echo ""
            echo "========================================="
            echo "    Running Verification Checks"
            echo "========================================="
            echo ""
            
            if [ -r "$JSON_PATH" ]; then echo "✓ Vulkan manifest exists and is readable"; else echo "✗ ERROR: Cannot read Vulkan manifest at $JSON_PATH"; exit 1; fi
            if grep -q "VK_LAYER_VKBASALT_RELOADED_post_processing" "$JSON_PATH"; then echo "✓ Vulkan manifest contains correct layer name"; else echo "✗ ERROR: Vulkan manifest does not contain expected layer name"; exit 1; fi
            if grep -q "/usr/lib/libVkLayer_Basalt_reloaded.so" "$JSON_PATH"; then echo "✓ Vulkan manifest contains correct library path"; else echo "✗ ERROR: Vulkan manifest does not contain expected library path"; exit 1; fi
            if python3 -c "import json; json.load(open('$JSON_PATH'))" 2>/dev/null; then echo "✓ Vulkan manifest is valid JSON"; else echo "✗ WARNING: Vulkan manifest may have JSON syntax errors"; fi
            if [ -f "$INSTALLED_SO" ]; then echo "✓ Library binary exists at $INSTALLED_SO"; else echo "✗ ERROR: Library binary not found at $INSTALLED_SO"; exit 1; fi
            if file "$INSTALLED_SO" | grep -q "shared object"; then echo "✓ Library is a valid shared object"; else echo "✗ ERROR: Library is not a valid shared object"; exit 1; fi
            
            echo ""
            echo "Installed binary: $INSTALLED_SO"
            echo "Size: $(du -h "$INSTALLED_SO" | cut -f1)"
            if file "$INSTALLED_SO" | grep -q 'stripped'; then
                echo "Status: symbols stripped ✓"
            else
                echo "Status: debug symbols retained"
            fi
            
            echo ""
            echo "Vulkan manifest: $JSON_PATH"
            echo "Size: $(du -h "$JSON_PATH" | cut -f1)"
            
            echo ""
            echo "========================================="
            echo "    All Checks Passed!"
            echo "========================================="
            echo ""
            echo "Installation complete!"
            echo "Please FULLY restart Steam (Right-click tray -> Exit) so the Proton container re-reads the manifest."
            ;;
        *)
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build [--strip]"
            ;;
    esac
else
    echo "Compilation failed. Check the errors above."
    exit 1
fi
