#!/usr/bin/env bash
set -euo pipefail

# Build the AE plugin for release.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

BUILD_TYPE="${1:-Release}"
AE_SDK_PATH="${AE_SDK_PATH:-$ROOT_DIR/ae_sdk}"

echo "=== Building CorridorKey AE Plugin (${BUILD_TYPE}) ==="

cd "$ROOT_DIR"
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAE_SDK_PATH="$AE_SDK_PATH"

cmake --build build --config "$BUILD_TYPE"

echo ""
echo "Build artifacts:"
find build -name "CorridorKey.*" -type f 2>/dev/null || echo "  (none found)"

echo ""
echo "=== Build complete ==="
