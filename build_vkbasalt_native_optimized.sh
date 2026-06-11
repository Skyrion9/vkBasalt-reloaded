#!/usr/bin/env bash

# 1. Clean previous state cleanly
rm -rf builddir build

# 2. Enforce Clang toolchain and LLD Linker using Bash exports
export CC=clang
export CXX=clang++
export CC_LD=lld
export CXX_LD=lld

# 3. Configure with ThinLTO, LLD, and native CPU optimizations packed into compiler args
meson setup build --buildtype=release \
  -Db_lto=true \
  -Db_lto_mode=thin \
  -Dc_args='-O3 -march=native' \
  -Dcpp_args='-O3 -march=native'

# 4. Compile using all available CPU threads via the Ninja backend
if meson compile -C build; then
    echo ""
    echo "========================================="
    echo "    Compilation Successful!   "
    echo "========================================="
    echo ""

    # 5. Prompt for immediate system installation
    read -p "Do you want to install vkBasalt right now? [y/N]: " confirm

    case "$confirm" in
        y|Y|yes|Yes|YES)
            echo "Running system installation..."
            sudo meson install -C build
            ;;
        *)
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build"
            ;;
    esac
else
    echo "Compilation failed. Check the errors above."
    exit 1
fi
