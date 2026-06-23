#!/usr/bin/env bash

# 1. Clean previous state cleanly - Let's not do this unless something is absolutely broken.
# rm -rf builddir build

# 2. Enforce Clang toolchain and LLD Linker
export CC=clang
export CXX=clang++
export CC_LD=lld
export CXX_LD=lld

# 3. Configure. 
# Note: -march=native is passed here. All other optimizations (ThinLTO, O3, C++20, C17, ndebug) 
# are now baked into the root meson.build default_options!
meson setup build --prefix=/usr \
  --buildtype=release \
  -Dc_args='-march=native' \
  -Dcpp_args='-march=native'

# 4. Compile
if meson compile -C build; then
    echo ""
    echo "========================================="
    echo "    Compilation Successful!   "
    echo "========================================="
    echo ""

    read -p "Do you want to install vkBasalt system-wide right now? [y/N]: " confirm

    case "$confirm" in
        y|Y|yes|Yes|YES)
            echo "Running system installation..."
            sudo meson install -C build
            sudo ldconfig

            # Patch the Vulkan Manifest to guarantee Proton finds it
            JSON_PATH="/usr/share/vulkan/implicit_layer.d/vkBasalt.json"
            if [ -f "$JSON_PATH" ]; then
                sudo sed -i 's|"library_path":.*|"library_path": "/usr/lib/libvkbasalt.so",|g' "$JSON_PATH"
                echo "Patched Vulkan manifest to point to /usr/lib/libvkbasalt.so"
            fi

            echo ""
            echo "Installation complete!"
            echo "Please FULLY restart Steam (Right-click tray -> Exit) so the Proton container re-reads the manifest. If it's not your first time installing, simply restart your game."
            ;;
        *)
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build"
            ;;
    esac
else
    echo "Compilation failed. Check the errors above."
    exit 1
fi
