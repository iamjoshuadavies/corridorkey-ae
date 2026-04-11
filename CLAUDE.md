# CorridorKey AE - Claude Code Context

## Project Overview
Native Adobe After Effects plugin for green-screen keying, based on CorridorKey.
Three-layer architecture: Host Plugin (C++) → Bridge (IPC) → Runtime (Python).

## Build Commands
```bash
# Configure and build plugin (macOS) — must specify SDK path
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/Users/schwar/Documents/corridorkey-ae/ae_sdk
cmake --build build

# Clean rebuild (needed when PiPL or flags change — AE caches aggressively)
rm -rf build && cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/Users/schwar/Documents/corridorkey-ae/ae_sdk && cmake --build build

# Run runtime tests
cd runtime && source .venv/bin/activate && python -m pytest tests/

# Run full test suite
scripts/bootstrap/run_tests.sh
```

## Architecture
- `plugin/` — C++ AE effect plugin (CMake build)
- `runtime/` — Python inference service (PyTorch/MLX)
- `shared/` — IPC protocol definitions and schemas
- `scripts/` — Bootstrap, packaging, release automation
- `docs/` — Architecture docs, PRD, research findings

## Key Conventions
- CMake 3.15+ for all C++ builds
- Plugin targets macOS Universal (Intel + ARM64) and Windows x64
- Runtime uses Python 3.10+
- IPC over local socket (msgpack framing)
- All model weights excluded from repo (downloaded at first run)
- Effect parameters follow AE Skeleton sample patterns

## AE Plugin Debugging

### Plugin Loading Log
AE writes a detailed plugin loading log on every launch:
```
~/Library/Preferences/Adobe/After Effects/26.0/Plugin Loading.log
```
Search for "CorridorKey" to see load status. Key messages:
- `"Loading ..."` — AE found the plugin
- `"The plugin is marked as Ignore"` — AE cached plugin as broken from a previous failed load. Fix: touch the binary + re-codesign, or clean rebuild, then restart AE.
- `"parameter count mismatch (X :: Y)"` — `PARAM_COUNT` enum doesn't match params registered in `SetupParams()`
- `"did not initialize max_result_rect in PF_Cmd_SMART_PRE_RENDER"` — declared `PF_OutFlag2_SUPPORTS_SMART_RENDER` but didn't implement smart render handlers

### Plugin Cache ("Ignore" state)
AE caches broken plugins and skips them on subsequent launches. To force a rescan:
1. Clean rebuild: `rm -rf build && cmake ...`
2. Touch the binary: `touch build/plugin/CorridorKey.plugin/Contents/MacOS/CorridorKey`
3. Re-codesign: `codesign --force --sign - --timestamp=none build/plugin/CorridorKey.plugin`
4. Nuclear option: hold **Cmd+Option+Shift** while launching AE to reset all preferences

### PiPL + OutFlags
The PiPL `.r` resource and `GlobalSetup` out_flags MUST match. If they disagree, AE may silently reject the plugin. When changing flags:
- Update `plugin/resources/CorridorKeyAEPiPL.r`
- Update `HandleGlobalSetup()` in `plugin/src/CorridorKeyAE.cpp`
- Do a clean rebuild (PiPL is compiled with Rez)

### Plugin Symlink (dev)
Plugin is symlinked into AE for live development:
```
/Applications/Adobe After Effects 2026/Plug-ins/Effects/CorridorKey.plugin
  → build/plugin/CorridorKey.plugin
```
Rebuilds auto-update. Restart AE to pick up changes.

## Licensing
NOT YET RESOLVED — see LICENSE file. Do not describe as "open source."
