#!/usr/bin/env bash
set -euo pipefail

# CorridorKey AE — Developer setup script
# Sets up both the C++ build environment and Python runtime.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== CorridorKey AE Developer Setup ==="
echo "Root: $ROOT_DIR"

# --- Python runtime ---
echo ""
echo "--- Setting up Python runtime ---"
cd "$ROOT_DIR/runtime"

if [ ! -d ".venv" ]; then
    echo "Creating virtual environment..."
    python3 -m venv .venv
fi

echo "Activating venv and installing dependencies..."
source .venv/bin/activate
pip install -e ".[dev]" --quiet

echo "Running runtime tests..."
python -m pytest tests/ -v

# --- C++ plugin ---
echo ""
echo "--- Checking C++ build prerequisites ---"

if ! command -v cmake &> /dev/null; then
    echo "WARNING: cmake not found. Install CMake 3.15+ to build the plugin."
else
    CMAKE_VERSION=$(cmake --version | head -1)
    echo "CMake: $CMAKE_VERSION"
fi

AE_SDK_PATH="${AE_SDK_PATH:-$ROOT_DIR/ae_sdk}"
if [ -d "$AE_SDK_PATH" ]; then
    echo "AE SDK found at: $AE_SDK_PATH"
else
    echo "WARNING: AE SDK not found at $AE_SDK_PATH"
    echo "  Set AE_SDK_PATH or place the SDK in ae_sdk/ at the repo root."
    echo "  Plugin will build in stub mode without it."
fi

echo ""
echo "--- Configuring CMake build ---"
cd "$ROOT_DIR"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug \
    -DAE_SDK_PATH="$AE_SDK_PATH" \
    2>&1 || echo "CMake configure failed (expected without AE SDK)"

echo ""
echo "=== Setup complete ==="
echo "  Runtime: cd runtime && source .venv/bin/activate"
echo "  Plugin:  cmake --build build"
