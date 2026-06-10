#!/usr/bin/env fish

# 1. Clean previous state cleanly
rm -rf builddir build

# 2. Enforce Clang toolchain and LLD Linker using Fish global exports
set -gx CC clang
set -gx CXX clang++
set -gx CC_LD lld
set -gx CXX_LD lld

# 3. Configure with ThinLTO, LLD, and native CPU optimizations packed into compiler args
meson setup build --buildtype=release \
  -Db_lto=true \
  -Db_lto_mode=thin \
  -Dc_args='-O3 -march=native' \
  -Dcpp_args='-O3 -march=native'

# 4. Compile using all available CPU threads via the Ninja backend
if meson compile -C build
    echo ""
    echo "========================================="
    echo "   Compilation Successful!   "
    echo "========================================="
    echo ""

    # 5. Prompt for immediate system installation
    read -l -P "Do you want to install vkBasalt right now? [y/N]: " confirm

    switch $confirm
        case y Y yes Yes YES
            echo "Running system installation..."
            sudo meson install -C build
        case '*'
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build"
    end
else
    echo "Compilation failed. Check the errors above."
    exit 1
end
