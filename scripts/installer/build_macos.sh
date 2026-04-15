#!/bin/bash
#
# Build the macOS .pkg installer for CorridorKey AE.
#
# Usage:
#   scripts/installer/build_macos.sh [--version 0.1.0]
#
# Expects:
#   - A built plugin bundle at build/plugin/CorridorKey.plugin
#     (run `cmake --build build` first, or provide --plugin-dir)
#   - pkgbuild + productbuild from Xcode Command Line Tools
#   - curl
#
# Produces:
#   dist/CorridorKey-<version>-macOS-arm64.pkg
#
# The .pkg ships:
#   /Library/Application Support/CorridorKey/python/     (python-build-standalone)
#   /Library/Application Support/CorridorKey/runtime/    (server/, engines/, models/, pyproject.toml)
#   /Library/Application Support/CorridorKey/installer/  (requirements.txt for postinstall)
#   /Library/Application Support/CorridorKey/plugin/CorridorKey.plugin  (for postinstall to copy into AE Plug-Ins)
#
# The .venv and pip install happen at install time via the postinstall
# script — we do NOT ship a pre-built venv, because venv symlinks bake
# in the absolute path of the Python used to create them, and on this
# CI runner that path won't match the user's machine.
#
# This script is written to work both locally (for dev iteration) and
# unchanged on a GitHub Actions macos-latest runner.
#

set -euo pipefail

VERSION="0.1.0"
PYTHON_BUILD_STANDALONE_TAG="20260414"
PYTHON_BUILD_STANDALONE_VERSION="3.12.13"
PLUGIN_DIR=""
OUT_DIR=""

# --- Args ---
while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --plugin-dir)
            PLUGIN_DIR="$2"
            shift 2
            ;;
        --out-dir)
            OUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,25p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# --- Resolve paths ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${PLUGIN_DIR:=$REPO_ROOT/build/plugin/CorridorKey.plugin}"
: "${OUT_DIR:=$REPO_ROOT/dist}"

if [ ! -d "$PLUGIN_DIR" ]; then
    echo "ERROR: plugin bundle not found at $PLUGIN_DIR" >&2
    echo "Build it first with: cmake --build build" >&2
    exit 1
fi

# --- Temp workspace ---
WORK="$(mktemp -d -t ck-pkg-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

PAYLOAD="$WORK/payload"                           # what pkgbuild will archive
SCRIPTS="$WORK/scripts"                           # pre/postinstall scripts
INSTALL_ROOT="$PAYLOAD/Library/Application Support/CorridorKey"

mkdir -p "$PAYLOAD" "$SCRIPTS" "$INSTALL_ROOT" "$OUT_DIR"

echo "=== CorridorKey AE .pkg builder ==="
echo "Version:      $VERSION"
echo "Plugin:       $PLUGIN_DIR"
echo "Work dir:     $WORK"
echo "Payload root: $PAYLOAD"
echo "Output:       $OUT_DIR"
echo ""

# --- 1. Download python-build-standalone ---
# Cache locally so re-runs don't re-download.
CACHE_DIR="$REPO_ROOT/.build-cache"
mkdir -p "$CACHE_DIR"

PYTHON_TARBALL="cpython-${PYTHON_BUILD_STANDALONE_VERSION}+${PYTHON_BUILD_STANDALONE_TAG}-aarch64-apple-darwin-install_only.tar.gz"
PYTHON_URL="https://github.com/astral-sh/python-build-standalone/releases/download/${PYTHON_BUILD_STANDALONE_TAG}/${PYTHON_TARBALL}"
PYTHON_CACHE="$CACHE_DIR/$PYTHON_TARBALL"

if [ ! -f "$PYTHON_CACHE" ]; then
    echo "Downloading $PYTHON_TARBALL ..."
    curl -sSL "$PYTHON_URL" -o "$PYTHON_CACHE.tmp"
    mv "$PYTHON_CACHE.tmp" "$PYTHON_CACHE"
fi
echo "Python tarball: $PYTHON_CACHE ($(du -h "$PYTHON_CACHE" | awk '{print $1}'))"

# --- 2. Extract python into the payload ---
echo "Extracting python into payload..."
tar -xzf "$PYTHON_CACHE" -C "$INSTALL_ROOT"
# tar extracts to "python/" — verify
if [ ! -x "$INSTALL_ROOT/python/bin/python3" ]; then
    echo "ERROR: python/bin/python3 not found after extract" >&2
    exit 1
fi

# --- 3. Copy runtime source code into the payload ---
echo "Copying runtime source code..."
mkdir -p "$INSTALL_ROOT/runtime"
rsync -a --exclude '__pycache__' --exclude '.venv' --exclude 'tests' \
    "$REPO_ROOT/runtime/server"  "$INSTALL_ROOT/runtime/"
rsync -a --exclude '__pycache__' --exclude '.venv' \
    "$REPO_ROOT/runtime/engines" "$INSTALL_ROOT/runtime/"
rsync -a --exclude '__pycache__' --exclude '.venv' \
    "$REPO_ROOT/runtime/models"  "$INSTALL_ROOT/runtime/"
cp "$REPO_ROOT/runtime/pyproject.toml" "$INSTALL_ROOT/runtime/"

# --- 4. Copy the requirements file to the payload (postinstall reads it) ---
mkdir -p "$INSTALL_ROOT/installer"
cp "$REPO_ROOT/installer/macos/requirements.txt" "$INSTALL_ROOT/installer/"

# --- 5. Copy the plugin bundle into the payload ---
# postinstall copies this to /Applications/Adobe After Effects*/Plug-Ins/Effects/
echo "Staging plugin bundle..."
mkdir -p "$INSTALL_ROOT/plugin"
rsync -a "$PLUGIN_DIR/" "$INSTALL_ROOT/plugin/CorridorKey.plugin/"

# --- 6. Copy the postinstall script ---
cp "$REPO_ROOT/installer/macos/scripts/postinstall" "$SCRIPTS/postinstall"
chmod +x "$SCRIPTS/postinstall"

# --- 7. Build the component .pkg ---
COMPONENT_PKG="$WORK/CorridorKey-component.pkg"
echo "Running pkgbuild..."
pkgbuild \
    --root "$PAYLOAD" \
    --identifier "com.corridorkey.ae" \
    --version "$VERSION" \
    --install-location "/" \
    --scripts "$SCRIPTS" \
    "$COMPONENT_PKG"

# --- 8. Wrap it in a distribution productbuild package ---
FINAL_PKG="$OUT_DIR/CorridorKey-${VERSION}-macOS-arm64.pkg"
DIST_XML="$WORK/Distribution.xml"

# Copy the template and patch ONLY the pkg-ref version (not minSpecVersion
# which also happens to have a version="..." attribute).
sed "s|<pkg-ref id=\"com.corridorkey.ae\" version=\"[0-9.]*\"|<pkg-ref id=\"com.corridorkey.ae\" version=\"$VERSION\"|" \
    "$REPO_ROOT/installer/macos/Distribution.xml" > "$DIST_XML"

echo "Running productbuild..."
productbuild \
    --distribution "$DIST_XML" \
    --package-path "$WORK" \
    "$FINAL_PKG"

# --- 9. Done ---
echo ""
echo "=== Build complete ==="
echo "Installer: $FINAL_PKG"
echo "Size:      $(du -h "$FINAL_PKG" | awk '{print $1}')"
echo ""
echo "To test locally:"
echo "  sudo installer -pkg '$FINAL_PKG' -target /"
echo "Or double-click in Finder."
