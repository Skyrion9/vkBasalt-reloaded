#!/usr/bin/env fish

cd (dirname (status -f))

# Enforce Clang toolchain + mold linker
set -gx CC clang
set -gx CXX clang++
set -gx CC_LD mold
set -gx CXX_LD mold

# Use ccache if available
if command -v ccache &>/dev/null
    set -gx CC "ccache clang"
    set -gx CXX "ccache clang++"
    echo "ccache detected and enabled."
end

# Configure
meson setup build --prefix=/usr \
  --buildtype=release \
  -Dc_args='-march=native' \
  -Dcpp_args='-march=native'

# Compile
if meson compile -C build
    echo ""
    echo "========================================="
    echo "   Compilation Successful!   "
    echo "========================================="
    echo ""

    read -l -P "Do you want to install vkBasalt system-wide right now? [y/N]: " confirm

    switch $confirm
        case y Y yes Yes YES
            echo "Running system installation..."
            sudo meson install -C build
            sudo ldconfig

            set json_path "/usr/share/vulkan/implicit_layer.d/vkBasalt.json"
            if test -f $json_path
                sudo sed -i 's|"library_path":.*|"library_path": "/usr/lib/libvkbasalt.so",|g' $json_path
                echo "Patched Vulkan manifest to point to /usr/lib/libvkbasalt.so"
            end

            echo ""
            echo "Installation complete!"
            echo "Please FULLY restart Steam (Right-click tray -> Exit) so the Proton container re-reads the manifest. If it's not your first time installing, simply restart your game."

        case '*'
            echo "Skipping installation. You can deploy it later using: sudo meson install -C build"
    end
else
    echo "Compilation failed. Check the errors above."
    exit 1
end
