# CorridorKey AE - Claude Code Context

## Project Overview
Native Adobe After Effects plugin for green-screen keying, based on CorridorKey.
Three-layer architecture: Host Plugin (C++) → Bridge (IPC) → Runtime (Python/MLX).
Created by Niko Pueringer / Corridor Digital. AE plugin wrapper by this project.

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

# Start runtime manually (for dev — auto-launch handles this normally)
cd runtime && source .venv/bin/activate && python -m server.main --port 12345
```

## Architecture
- `plugin/` — C++ AE effect plugin (CMake build, Drawbot UI, Smart Render)
- `runtime/` — Python inference service (corridorkey_mlx, tiled inference)
- `shared/` — IPC protocol definitions and schemas
- `scripts/` — Bootstrap, packaging, release automation
- `docs/` — Architecture docs, PRD, research findings

### IPC Protocol
Length-prefixed binary messages over TCP (127.0.0.1, auto-assigned port).
- JSON text messages: ping, status, shutdown
- Binary FRAME messages: `"FRAME"` magic + dimensions + params + pixel data + optional alpha hint
- Response: `"FRAME"` magic + dimensions + processed pixel data
- All pixel data is ARGB 8bpc (converted from/to project bit depth in C++)

### Inference Pipeline
- Model: CorridorKey via `corridorkey_mlx` package (pip from GitHub)
- Mode: **Tiled inference** (tile_size=512, overlap=64) for full-resolution output
- Performance: ~4.6s/frame at 1920×1080 on M1, ~0.3s at 512×512
- Model weights: `~/Library/Application Support/EZ-CorridorKey/CorridorKeyModule/checkpoints/corridorkey_mlx.safetensors`
- img_size=2048 is NOT viable on M1 (~450s/frame). Tiled mode at 512 is the correct approach.

## Key Conventions
- CMake 3.15+ for all C++ builds
- Plugin targets macOS Universal (Intel + ARM64) and Windows x64
- Runtime uses Python 3.10+ (3.12 via Homebrew on this machine)
- IPC over local TCP socket (length-prefixed binary framing)
- All model weights excluded from repo (downloaded at first run)
- PiPL flags and GlobalSetup out_flags MUST match exactly
- Smart Render required for 32bpc float support
- MFR (Multi-Frame Rendering) enabled — bridge uses mutex to serialize

## Current Features
- Smart Render: 8bpc, 16bpc, 32bpc float
- Multi-Frame Rendering (threaded, serialized via mutex)
- Auto-launch runtime subprocess (fork/exec, PORT discovery from stdout)
- Reconnect with exponential backoff cooldown
- Zombie process cleanup on re-launch
- Custom Drawbot UI: logo, title, tagline, clickable About link
- Alpha hint layer input (PF_ADD_LAYER)
- Output modes: Processed, Matte, Foreground, Composite
- Effect params: Device, Quality, Low Memory, Despill, Despeckle, Refiner, Matte Cleanup
- Tiled inference for full-resolution output
- Debug image saves gated behind CK_DEBUG=1 env var

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
- `"no custom ui outflag"` — using PF_PUI_CONTROL without PF_OutFlag_CUSTOM_UI in GlobalSetup
- `"PF_OutFlag2_FLOAT_COLOR_AWARE requires PF_OutFlag2_SUPPORTS_SMART_RENDER"` — need smart render for 32bpc

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

Current flags:
- `out_flags`: PIX_INDEPENDENT | DEEP_COLOR_AWARE | CUSTOM_UI
- `out_flags2`: SUPPORTS_SMART_RENDER | FLOAT_COLOR_AWARE | SUPPORTS_THREADED_RENDERING

### Plugin Symlink (dev)
Plugin is symlinked into AE for live development:
```
/Applications/Adobe After Effects 2026/Plug-ins/Effects/CorridorKey.plugin
  → build/plugin/CorridorKey.plugin
```
Rebuilds auto-update. Restart AE to pick up changes.

### Crash/Hang Logs
```
/Library/Logs/DiagnosticReports/After Effects_*.diag   # crashes
/Library/Logs/DiagnosticReports/After Effects_*.hang   # hangs/deadlocks
```

## Licensing
PolyForm Noncommercial 1.0.0 — source-available, free for non-commercial use.
Users CAN use the tool in commercial VFX work. Users CANNOT sell the tool itself.
Do not describe as "open source" — it is "source-available" under a non-commercial license.
