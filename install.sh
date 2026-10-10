#!/bin/bash
set -euo pipefail

# ============================================================================
# vkBasalt Reloaded - Standalone Installer
# Ships alongside pre-built libvkbasalt.so binaries.
# Usage: ./install.sh [--prefix=/usr] [--uninstall]
# ============================================================================

INSTALL_LIB_DIR="/usr/lib"
INSTALL_MANIFEST_DIR="/usr/share/vulkan/implicit_layer.d"
MANIFEST_NAME="vkBasalt.json"
LIB_NAME="libvkbasalt.so"

# Parse arguments
PREFIX="/usr"
UNINSTALL=false

for arg in "$@"; do
    case "$arg" in
        --prefix=*)    PREFIX="${arg#*=}" ;;
        --uninstall)   UNINSTALL=true ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --prefix=PATH  Installation prefix (default: /usr)"
            echo "  --uninstall    Remove vkBasalt installation"
            echo "  --help, -h     Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg (use --help for usage)"
            exit 1
            ;;
    esac
done

INSTALL_LIB_DIR="$PREFIX/lib"

# Privilege escalation
if [ "$EUID" -ne 0 ]; then
    if ! command -v sudo &>/dev/null; then
        echo "ERROR: Root privileges required. Run with sudo."
        exit 1
    fi
    SUDO="sudo"
else
    SUDO=""
fi

# ============================================================================
# Uninstall path
# ============================================================================
if [ "$UNINSTALL" = true ]; then
    echo "=== vkBasalt Reloaded Uninstaller ==="
    echo ""
    read -p "Remove vkBasalt from $INSTALL_LIB_DIR and $INSTALL_MANIFEST_DIR? [y/N]: " confirm
    case "$confirm" in
        y|Y|yes|Yes|YES)
            if [ -f "$INSTALL_LIB_DIR/$LIB_NAME" ]; then
                $SUDO rm -f "$INSTALL_LIB_DIR/$LIB_NAME"
                echo "✓ Removed $INSTALL_LIB_DIR/$LIB_NAME"
            else
                echo "  $INSTALL_LIB_DIR/$LIB_NAME not found, skipping."
            fi
            if [ -f "$INSTALL_MANIFEST_DIR/$MANIFEST_NAME" ]; then
                $SUDO rm -f "$INSTALL_MANIFEST_DIR/$MANIFEST_NAME"
                echo "✓ Removed $INSTALL_MANIFEST_DIR/$MANIFEST_NAME"
            else
                echo "  $INSTALL_MANIFEST_DIR/$MANIFEST_NAME not found, skipping."
            fi
            $SUDO ldconfig
            echo "✓ ldconfig updated"
            echo ""
            echo "Uninstall complete. Restart Steam to apply changes."
            ;;
        *)
            echo "Aborted."
            ;;
    esac
    exit 0
fi

# ============================================================================
# Install path
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_SO="$SCRIPT_DIR/$LIB_NAME"

echo "=== vkBasalt Reloaded Installer ==="
echo ""

# Locate the binary
if [ ! -f "$SOURCE_SO" ]; then
    echo "ERROR: $LIB_NAME not found in $SCRIPT_DIR"
    echo "Place the .so file in the same directory as this script."
    exit 1
fi

echo "Source binary: $SOURCE_SO"
echo "Install target: $INSTALL_LIB_DIR/$LIB_NAME"
echo ""

# Show binary info
echo "--- Binary Info ---"
file "$SOURCE_SO"
echo "Size: $(du -h "$SOURCE_SO" | cut -f1)"
echo ""

# Show BUILD_INFO if present (CI artifacts include this)
if [ -f "$SCRIPT_DIR/BUILD_INFO.txt" ]; then
    echo "--- Build Information ---"
    cat "$SCRIPT_DIR/BUILD_INFO.txt"
    echo ""
fi

# Optional symbol stripping
read -p "Strip debug symbols? [Y/n]: " strip_confirm
if [[ ! "$strip_confirm" =~ ^[Nn] ]]; then
    STRIP=true
    echo "Installation will strip debug symbols."
else
    STRIP=false
    echo "Installation will keep debug symbols."
fi

# Backup existing installation
BACKUP_DIR=$(mktemp -d)
trap 'rm -rf "$BACKUP_DIR"' EXIT

if [ -f "$INSTALL_LIB_DIR/$LIB_NAME" ]; then
    echo "Backing up existing $LIB_NAME..."
    cp "$INSTALL_LIB_DIR/$LIB_NAME" "$BACKUP_DIR/$LIB_NAME.bak"
fi
if [ -f "$INSTALL_MANIFEST_DIR/$MANIFEST_NAME" ]; then
    cp "$INSTALL_MANIFEST_DIR/$MANIFEST_NAME" "$BACKUP_DIR/$MANIFEST_NAME.bak"
fi

# Strip if requested
INSTALL_SO="$SOURCE_SO"
STRIPPED_TMP=""
if [ "$STRIP" = true ]; then
    echo "Stripping debug symbols..."
    STRIPPED_TMP=$(mktemp /tmp/vkbasalt-stripped-XXXXXX.so)
    cp "$SOURCE_SO" "$STRIPPED_TMP"
    strip "$STRIPPED_TMP"
    INSTALL_SO="$STRIPPED_TMP"
    echo "Stripped size: $(du -h "$STRIPPED_TMP" | cut -f1)"
fi

# Install the library
echo ""
echo "Installing..."
$SUDO mkdir -p "$INSTALL_LIB_DIR"
$SUDO cp "$INSTALL_SO" "$INSTALL_LIB_DIR/$LIB_NAME"
$SUDO chmod 755 "$INSTALL_LIB_DIR/$LIB_NAME"
echo "✓ Installed $INSTALL_LIB_DIR/$LIB_NAME"

# Write the Vulkan layer manifest
$SUDO mkdir -p "$INSTALL_MANIFEST_DIR"
cat << MANIFEST | $SUDO tee "$INSTALL_MANIFEST_DIR/$MANIFEST_NAME" > /dev/null
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_VKBASALT_post_processing",
        "type": "GLOBAL",
        "library_path": "$INSTALL_LIB_DIR/$LIB_NAME",
        "api_version": "1.3.223",
        "implementation_version": "1",
        "description": "a post processing layer",
        "functions": {
            "vkGetInstanceProcAddr": "vkBasalt_GetInstanceProcAddr",
            "vkGetDeviceProcAddr": "vkBasalt_GetDeviceProcAddr"
        },
        "enable_environment": {
            "ENABLE_VKBASALT_RELOADED": "1"
        },
        "disable_environment": {
            "DISABLE_VKBASALT_RELOADED": "1"
        }
    }
}
MANIFEST
echo "✓ Installed $INSTALL_MANIFEST_DIR/$MANIFEST_NAME"

# Update linker cache
$SUDO ldconfig
echo "✓ ldconfig updated"

# Cleanup stripped temp
if [ -n "$STRIPPED_TMP" ] && [ -f "$STRIPPED_TMP" ]; then
    rm -f "$STRIPPED_TMP"
fi

# Verify installation
echo ""
echo "========================================="
echo "    Installation Complete!"
echo "========================================="
echo ""
INSTALLED_SO="$INSTALL_LIB_DIR/$LIB_NAME"
echo "Installed: $INSTALLED_SO ($(du -h "$INSTALLED_SO" | cut -f1))"
if file "$INSTALLED_SO" | grep -q 'stripped'; then
    echo "Status: symbols stripped ✓"
else
    echo "Status: debug symbols retained"
fi
echo ""
echo "To enable vkBasalt for a game, add to Steam launch options:"
echo "  ENABLE_VKBASALT_RELOADED=1 %command%"
echo ""
echo "Restart Steam fully (tray → Exit) so Proton re-reads the manifest."
