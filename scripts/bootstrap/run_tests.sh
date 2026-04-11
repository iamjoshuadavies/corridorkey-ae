#!/usr/bin/env bash
set -euo pipefail

# Run all tests for CorridorKey AE.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== CorridorKey AE Test Suite ==="

# --- Python runtime tests ---
echo ""
echo "--- Runtime tests ---"
cd "$ROOT_DIR/runtime"

if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

python -m pytest tests/ -v --tb=short

echo ""
echo "--- Lint check ---"
python -m ruff check server/ engines/ models/ tests/ || true

echo ""
echo "=== All tests complete ==="
