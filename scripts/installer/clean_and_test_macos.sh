#!/bin/bash
#
# Clean-slate + install-test harness for the macOS .pkg.
#
# Wipes every CorridorKey artifact the installer or dev workflow could
# have left on the machine, then installs a .pkg and verifies the result.
# Lets you go from "worked on my machine" to "actually fresh install"
# in one command.
#
# Usage:
#   scripts/installer/clean_and_test_macos.sh                    # clean only
#   scripts/installer/clean_and_test_macos.sh --pkg PATH         # clean + install local .pkg
#   scripts/installer/clean_and_test_macos.sh --from-ci          # clean + download+install latest CI .pkg
#   scripts/installer/clean_and_test_macos.sh --from-ci --run ID # clean + install .pkg from a specific CI run
#   scripts/installer/clean_and_test_macos.sh --clean-only       # just clean, skip install
#
# Requires sudo (you'll be prompted once). Close After Effects first.
#

set -euo pipefail

MODE="clean-and-install"
PKG_PATH=""
CI_RUN_ID=""
FROM_CI=0

while [ $# -gt 0 ]; do
    case "$1" in
        --pkg)          PKG_PATH="$2"; shift 2 ;;
        --from-ci)      FROM_CI=1; shift ;;
        --run)          CI_RUN_ID="$2"; shift 2 ;;
        --clean-only)   MODE="clean-only"; shift ;;
        -h|--help)      sed -n '2,22p' "$0"; exit 0 ;;
        *)              echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

REPO="iamjoshuadavies/corridorkey-ae"

say()  { printf "\033[1;36m==>\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m!! \033[0m %s\n" "$*"; }
ok()   { printf "\033[1;32m✓\033[0m  %s\n" "$*"; }
fail() { printf "\033[1;31m✗\033[0m  %s\n" "$*"; exit 1; }

# --- Preflight --------------------------------------------------------------
if pgrep -x "After Effects" >/dev/null 2>&1; then
    fail "After Effects is running. Quit it and re-run this script."
fi

say "Requesting sudo up front so the rest of the script is non-interactive"
sudo -v

# Keep sudo alive while the script runs.
( while true; do sudo -n true; sleep 30; kill -0 "$$" 2>/dev/null || exit; done ) &
SUDO_KEEPALIVE=$!
trap 'kill "$SUDO_KEEPALIVE" 2>/dev/null || true' EXIT

# --- 1. Kill any stray runtime ----------------------------------------------
say "Killing any running CorridorKey runtime processes"
if pgrep -f "server.main" >/dev/null 2>&1; then
    pkill -f "server.main" || true
    ok "Killed running runtime(s)"
else
    ok "No runtime processes running"
fi

# --- 2. Remove the plugin from every AE install -----------------------------
say "Removing CorridorKey.plugin from all AE Plug-ins/Effects folders"
shopt -s nullglob
for ae in "/Applications/Adobe After Effects "*; do
    target="$ae/Plug-ins/Effects/CorridorKey.plugin"
    if [ -e "$target" ] || [ -L "$target" ]; then
        sudo rm -rf "$target"
        ok "Removed: $target"
    fi
done
shopt -u nullglob

# --- 3. System-wide install tree from the .pkg ------------------------------
say "Removing /Library/Application Support/CorridorKey (system install root)"
sudo rm -rf "/Library/Application Support/CorridorKey"
sudo rm -f  "/Library/Logs/CorridorKey-install.log"
ok "System install tree and log removed"

# --- 4. User-level dev artifacts --------------------------------------------
say "Removing user-level dev caches"
rm -rf "$HOME/Library/Application Support/CorridorKey"
rm -rf "$HOME/Library/Application Support/CorridorKey-models-backup"
ok "User caches removed"

# --- 5. pkgutil receipt -----------------------------------------------------
say "Forgetting pkgutil receipt so the next install is treated as fresh"
sudo pkgutil --forget com.corridorkey.ae >/dev/null 2>&1 || true
ok "Receipt cleared"

# --- 6. Port handoff file ---------------------------------------------------
rm -f /tmp/corridorkey_runtime.port /tmp/corridorkey_runtime.log || true

# --- 7. Warn about dev escape hatch -----------------------------------------
if [ -n "${CORRIDORKEY_REPO_ROOT:-}" ]; then
    warn "CORRIDORKEY_REPO_ROOT is set in your environment ($CORRIDORKEY_REPO_ROOT)."
    warn "The bridge will prefer that over the installed runtime. Unset it for a true test:"
    warn "    unset CORRIDORKEY_REPO_ROOT"
fi

# --- Verify clean state -----------------------------------------------------
say "Verifying clean state"
found_any=0
shopt -s nullglob
for ae in "/Applications/Adobe After Effects "*; do
    if [ -e "$ae/Plug-ins/Effects/CorridorKey.plugin" ]; then
        warn "Still present: $ae/Plug-ins/Effects/CorridorKey.plugin"
        found_any=1
    fi
done
shopt -u nullglob
[ -e "/Library/Application Support/CorridorKey" ] && { warn "Still present: /Library/Application Support/CorridorKey"; found_any=1; }
[ -e "$HOME/Library/Application Support/CorridorKey" ] && { warn "Still present: ~/Library/Application Support/CorridorKey"; found_any=1; }
pkgutil --pkgs | grep -q '^com\.corridorkey\.ae$' && { warn "pkgutil still knows about com.corridorkey.ae"; found_any=1; }

if [ "$found_any" -eq 0 ]; then
    ok "Clean state verified"
else
    fail "Some artifacts still present — see warnings above"
fi

if [ "$MODE" = "clean-only" ]; then
    echo ""
    ok "Clean-only mode: done."
    exit 0
fi

# --- 8. Resolve the .pkg to install -----------------------------------------
if [ "$FROM_CI" -eq 1 ]; then
    command -v gh >/dev/null 2>&1 || fail "gh CLI not found but --from-ci was requested."

    if [ -z "$CI_RUN_ID" ]; then
        say "Finding most recent successful CI run that produced a .pkg"
        CI_RUN_ID=$(gh run list --repo "$REPO" --workflow=CI --status success \
            --json databaseId,conclusion --jq '.[0].databaseId')
        [ -n "$CI_RUN_ID" ] || fail "No successful CI runs found."
        ok "Using CI run $CI_RUN_ID"
    fi

    DL_DIR=$(mktemp -d -t ck-pkg-dl-XXXXXX)
    say "Downloading CorridorKey-Installer-macOS artifact to $DL_DIR"
    gh run download "$CI_RUN_ID" --repo "$REPO" -n CorridorKey-Installer-macOS -D "$DL_DIR"
    PKG_PATH=$(ls "$DL_DIR"/CorridorKey-*.pkg 2>/dev/null | head -1)
    [ -n "$PKG_PATH" ] || fail "No .pkg found inside the artifact."
    ok "Downloaded: $PKG_PATH"
fi

if [ -z "$PKG_PATH" ]; then
    # Fall back to a local build.
    LOCAL_PKG=$(ls "$(git rev-parse --show-toplevel 2>/dev/null || echo .)"/dist/CorridorKey-*.pkg 2>/dev/null | head -1 || true)
    if [ -n "$LOCAL_PKG" ]; then
        PKG_PATH="$LOCAL_PKG"
        say "No --pkg given, using most recent local build: $PKG_PATH"
    else
        fail "No .pkg specified. Pass --pkg PATH, --from-ci, or build one first with scripts/installer/build_macos.sh"
    fi
fi

[ -f "$PKG_PATH" ] || fail "Not a file: $PKG_PATH"

# --- 9. Install -------------------------------------------------------------
say "Installing $PKG_PATH (this takes a minute while postinstall pip-installs deps)"
sudo installer -pkg "$PKG_PATH" -target /
ok "installer exited successfully"

# --- 10. Verify install -----------------------------------------------------
say "Verifying install"

INSTALL_ROOT="/Library/Application Support/CorridorKey"
checks=(
    "$INSTALL_ROOT/python/bin/python3"
    "$INSTALL_ROOT/runtime/.venv/bin/python3"
    "$INSTALL_ROOT/runtime/server/main.py"
    "$INSTALL_ROOT/runtime/engines"
    "$INSTALL_ROOT/plugin/CorridorKey.plugin/Contents/MacOS/CorridorKey"
    "$INSTALL_ROOT/VERSION"
)
for path in "${checks[@]}"; do
    if [ -e "$path" ]; then
        ok "Found: $path"
    else
        fail "Missing: $path"
    fi
done

# Plugin landed in at least one AE install?
plugin_installed=0
shopt -s nullglob
for ae in "/Applications/Adobe After Effects "*; do
    if [ -e "$ae/Plug-ins/Effects/CorridorKey.plugin/Contents/MacOS/CorridorKey" ]; then
        ok "Plugin installed into: $ae"
        plugin_installed=1
    fi
done
shopt -u nullglob
if [ "$plugin_installed" -eq 0 ]; then
    warn "No AE install had CorridorKey.plugin copied in — check /Library/Logs/CorridorKey-install.log"
fi

# Runtime import sanity check (proves the venv is actually usable).
say "Running import sanity check inside the installed venv"
if "$INSTALL_ROOT/runtime/.venv/bin/python3" -c \
    "import corridorkey_mlx, numpy, PIL, cv2, msgpack; print('imports ok')"; then
    ok "Runtime venv imports work"
else
    fail "Runtime venv import check failed"
fi

echo ""
ok "Install verified."
echo ""
echo "Next steps:"
echo "  1. Launch After Effects"
echo "  2. Apply Effect ▸ Keying ▸ CorridorKey to a green-screen layer"
echo "  3. First frame will pause while model weights download into"
echo "     $INSTALL_ROOT/models/"
echo ""
echo "Logs:"
echo "  /Library/Logs/CorridorKey-install.log   (postinstall)"
echo "  /tmp/corridorkey_runtime.log            (runtime, fresh each launch)"
