#!/usr/bin/env bash
set -euo pipefail

# Lower build process priority (don't hog the system while compiling)
renice -n 19 -p $$ >/dev/null 2>&1 || true
ionice -c 3 -p $$ >/dev/null 2>&1 || true

cd "$(dirname "${BASH_SOURCE[0]}")"

# Enforce Clang toolchain + mold linker (3-5x faster than lld for linking)
export CC=clang
export CXX=clang++
export CC_LD=mold
export CXX_LD=mold

# Use ccache if available (10x faster incremental rebuilds)
if command -v ccache &>/dev/null; then
    export CC="ccache clang"
    export CXX="ccache clang++"
    echo "ccache detected and enabled."
fi

# Configure
# -march=native: Targets your exact CPU microarchitecture
# All other flags (O3, ThinLTO, visibility, ndebug, C++20) are in meson.build
meson setup build --prefix=/usr \
  --buildtype=release \
  -Dc_args='-march=native' \
  -Dcpp_args='-march=native'

# Compile
if meson compile -C build; then
    echo ""
    echo "========================================="
    echo "    Compilation Successful!   "
    echo "========================================="
    echo ""

    read -p "Do you want to install vkBasalt system-wide right now? [y/N]: " confirm

    case "$confirm" in
        y|Y|yes|Yes|YES)
            read -p "Do you want to strip debug symbols from the binary? [Y/n]: " strip_confirm
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

            # Patch the Vulkan Manifest to guarantee Proton finds it
            JSON_PATH="/usr/share/vulkan/implicit_layer.d/vkBasalt.json"
            if [ -f "$JSON_PATH" ]; then
                sudo sed -i 's|"library_path":.*|"library_path": "/usr/lib/libvkbasalt.so",|g' "$JSON_PATH"
                echo "Patched Vulkan manifest to point to /usr/lib/libvkbasalt.so"
            fi

            # Verify strip succeeded
            INSTALLED_SO="/usr/lib/libvkbasalt.so"
            if [ -f "$INSTALLED_SO" ]; then
                echo ""
                echo "Installed binary: $INSTALLED_SO"
                echo "Size: $(du -h "$INSTALLED_SO" | cut -f1)"
                if file "$INSTALLED_SO" | grep -q 'stripped'; then
                    echo "Status: symbols stripped ✓"
                else
                    echo "Status: debug symbols retained"
                fi
            fi

            echo ""
            echo "Installation complete!"
            echo "Please FULLY restart Steam (Right-click tray -> Exit) so the Proton container re-reads the manifest. If it's not your first time installing, simply restart your game."
            ;;
        *)
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build [--strip]"
            ;;
    esac
else
    echo "Compilation failed. Check the errors above."
    exit 1
fi
