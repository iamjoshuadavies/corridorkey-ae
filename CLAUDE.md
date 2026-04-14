# CorridorKey AE - Claude Code Context

## Project Overview
Native Adobe After Effects plugin for green-screen keying, based on CorridorKey.
Three-layer architecture: Host Plugin (C++) → Bridge (IPC) → Runtime (Python/MLX).
Created by Niko Pueringer / Corridor Digital. AE plugin wrapper by this project.

## Build Commands

### macOS
```bash
# Configure and build plugin — must specify SDK path
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

### Windows
```bash
# Configure with VS 2019 generator (matches BuildTools 2019 on this machine).
# AE_SDK_PATH points at the Examples folder of the unpacked Win SDK; CMake
# also auto-detects a nested ae_sdk_win/<zip>/<zstd>/Examples layout if you
# don't pass it explicitly.
cmake -B build_win -S . -G "Visual Studio 16 2019" -A x64 \
      -DAE_SDK_PATH="C:/Users/iamjo/Documents/corridorkey-ae/ae_sdk_win/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK/Examples"
cmake --build build_win --config Release

# Install the .aex into AE's plug-ins folder. Program Files needs admin —
# run this from an elevated PowerShell with AE closed (the file is locked
# while AE is open).
Copy-Item -Force "build_win/plugin/Release/CorridorKey.aex" \
  "C:/Program Files/Adobe/Adobe After Effects 2026/Support Files/Plug-ins/Effects/CorridorKey.aex"

# Set the env var that lets the bridge find the runtime venv (because the
# .aex sits in Program Files with no relationship to the source repo).
[Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', 'C:\Users\iamjo\Documents\corridorkey-ae', 'User')

# Runtime venv (no MLX on Windows yet — minimal deps for fallback overlay)
cd runtime
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install msgpack numpy Pillow opencv-python-headless

# Start runtime manually
.\.venv\Scripts\python.exe -m server.main --port 12345
```

Claude's bash session is not elevated even when Claude Desktop is launched
as admin (sandbox runs at medium integrity). The `Copy-Item` step into
Program Files must be run by the user from an admin shell.

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

### Port handoff
The runtime writes `<pid> <port>\n` to `<temp>/corridorkey_runtime.port`
after binding. The C++ bridge polls that file to discover the port. macOS
also still parses `PORT:<n>` from the child's stdout pipe as a fallback
(the file is the primary mechanism on Windows because the Python venv
launcher chain swallows stdout before it reaches the parent's pipe).

### Inference Pipeline
- macOS: **MLX engine** via `corridorkey_mlx` package (pip from GitHub).
  Tiled inference (tile_size=512, overlap=64). ~4.6s/frame at 1080p,
  ~0.3s at 512×512. Weights cached at
  `~/Library/Application Support/CorridorKey/models/corridorkey_mlx.safetensors`,
  auto-downloaded from the upstream GitHub release on first run.
  img_size=2048 is NOT viable on M1 (~450s/frame).
- Windows: **PyTorch engine** (`runtime/engines/pytorch_engine.py`).
  Self-contained — uses a vendored `GreenFormer` (`_greenformer.py`,
  cleanly reimplemented from upstream `corridorkey-mlx`'s reference
  dump script) and weights loader (`_weights_loader.py`) that auto-
  downloads the official MLX safetensors from the corridorkey-mlx
  GitHub release on first run, applies the inverse of upstream's
  PyTorch→MLX converter (transpose conv kernels NCHW↔NHWC + rename
  refiner stem keys), and loads them strict into the vendored model.
  Cache lives at `%LOCALAPPDATA%\CorridorKey\models\`.
  Discovery order: `CORRIDORKEY_PT_WEIGHTS` env (escape hatch) → cache
  → fresh download. Fully self-hosted — no external tool required.
  Multi-resolution model cache wired to the Quality dropdown:
  Fastest=512 (no refiner), Fast=512, High=1024, Full Res=2048. Per
  resolution the GreenFormer is built lazily and pos_embed is bicubic-
  interpolated from the checkpoint's native size. All three sizes
  combined use ~0.5 GB VRAM. Steady-state on RTX 4090 fp16:
  Fastest ~165 ms, Fast ~173 ms, High ~234 ms, Full Res ~558 ms.
- Both engines apply ImageNet normalization to RGB inputs (mean
  `[0.485, 0.456, 0.406]`, std `[0.229, 0.224, 0.225]`). Skipping this
  is what produces washed-out / "milky" foreground output — the Hiera
  backbone needs the normalized distribution. The alpha-hint channel
  is NOT normalized (it's a mask, not color data).

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
- Auto-launch runtime subprocess (fork/exec on macOS, CreateProcessW on
  Windows; port discovery via temp file, with stdout pipe as macOS fallback)
- Reconnect with exponential backoff cooldown
- Zombie process cleanup on re-launch
- Custom Drawbot UI: logo, title, tagline, clickable About link
- Alpha hint layer input (PF_ADD_LAYER)
- Output modes: Processed, Matte, Foreground, Composite
- Effect params: Output Mode, Alpha Hint, Quality, Despill, Despeckle, Refiner, Matte Cleanup
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

## Windows-specific debugging

### Plugin loading log
```
%AppData%\Adobe\After Effects\26.0\Plugin Loading.log
```
Same error catalogue as macOS. Two latent PiPL bugs in the original `.r`
file silently passed AE on macOS but were rejected by Windows AE: the
`out_flags` literal had unrelated bits set (`0x04008040` instead of
`0x02008400`), and the `AE_Effect_Version` literal didn't match what
`PF_VERSION(0,1,0,DEVELOP,0)` produces (`32768`, not `65536`). Both are
fixed in `CorridorKeyAEPiPL.r` — keep them in sync if you bump the version.

### Runtime log
The runtime writes a fresh log each launch to:
```
%TEMP%\corridorkey_runtime.log
```
This is the only way to see runtime output when the bridge auto-launches
it — `CreateProcessW` runs the runtime with `CREATE_NO_WINDOW` and no
attached console, so stdout/stderr are not visible.

### Port handoff file
```
%TEMP%\corridorkey_runtime.port
```
Contents: `<pid> <port>\n`. Stale files are wiped by the bridge before
each `LaunchRuntime` call.

### Socket timeouts gotcha
Windows `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)` takes a `DWORD` of
**milliseconds**, not a POSIX `struct timeval`. Passing `tv_sec=30,
tv_usec=0` causes Windows to read the first 4 bytes as a DWORD = 30,
producing a 30-millisecond timeout. The bridge has a `SetSocketTimeoutMs`
helper that does the right thing on each platform — use it, never call
`setsockopt` for these directly.

### Plugin install path
```
C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Effects\CorridorKey.aex
```
Writing to Program Files needs an elevated shell. Symlinks would need
Developer Mode or admin too, so dev workflow is build → manual `Copy-Item`
→ restart AE.

### Build system gotchas
- VS 2019 16.11 BuildTools is what's installed and registered (`vswhere`
  doesn't see the partial 2022 install in `C:\Program Files (x86)\...\2022`).
  Use `-G "Visual Studio 16 2019"`. C++17 works fine in 14.29.
- The Windows PiPL pipeline is `cl /EP` → `PiPLtool.exe` → `cl /EP`,
  emitting `CorridorKeyAEPiPL_temp.rc` into the build dir. The tracked
  `plugin/resources/CorridorKeyAE.rc` is just `#include` of that — the
  build dir is added to the rc.exe include path so it resolves.

## Licensing
PolyForm Noncommercial 1.0.0 — source-available, free for non-commercial use.
Users CAN use the tool in commercial VFX work. Users CANNOT sell the tool itself.
Do not describe as "open source" — it is "source-available" under a non-commercial license.
