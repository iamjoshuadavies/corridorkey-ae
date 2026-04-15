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

# Bridge discovers the runtime venv via:
#   1. CORRIDORKEY_REPO_ROOT env var      (dev escape hatch)
#   2. %LOCALAPPDATA%\CorridorKey         (per-user install)
#   3. %ProgramFiles%\CorridorKey         (system-wide install)
# For a source-tree dev workflow set the env var once:
[Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', 'C:\Users\iamjo\Documents\corridorkey-ae', 'User')

# Runtime venv (Windows PyTorch + CUDA + model deps)
cd runtime
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install msgpack numpy Pillow opencv-python-headless timm safetensors
.\.venv\Scripts\python.exe -m pip install torch==2.5.1+cu121 torchvision==0.20.1+cu121 --index-url https://download.pytorch.org/whl/cu121

# Start runtime manually
.\.venv\Scripts\python.exe -m server.main --port 12345
```

Claude's bash session is not elevated even when Claude Desktop is launched
as admin (sandbox runs at medium integrity). The `Copy-Item` step into
Program Files must be run by the user from an admin shell.

## Installers (feature/installers branch — WIP, issue #28)

**Status:** Both platforms done end-to-end. macOS `.pkg` via
`pkgbuild`/`productbuild`, Windows `.exe` via InnoSetup 6. Both build
from the same plugin artifact in CI, both ship
python-build-standalone + runtime source + requirements and create
the venv on the user's machine at install time. Work lives on the
`feature/installers` branch, not yet merged to main — waiting until
both platforms are verified in the wild.

### Done (macOS)
- `scripts/installer/build_macos.sh` — stages `python-build-standalone`
  3.12.13 aarch64 + runtime source + plugin bundle + `requirements.txt`
  into a temp tree, runs `pkgbuild` + `productbuild`, drops a
  ~17 MB `CorridorKey-<ver>-macOS-arm64.pkg` in `dist/`.
- `installer/macos/Distribution.xml` — productbuild wrapper.
  `localSystem` domain (NOT per-user) because AE on macOS only scans
  5 system paths under `/Applications/Adobe After Effects */Plug-Ins/`,
  so there's no friction-free user install. One admin prompt at
  install time is unavoidable.
- `installer/macos/scripts/postinstall` — runs as root after payload
  is laid down at `/Library/Application Support/CorridorKey/`. Creates
  venv (default symlink mode — NOT `--copies`, that breaks
  `@rpath/libpython3.12.dylib` lookup), pip-installs runtime deps,
  finds the highest AE version via `/Applications/Adobe After Effects *`
  glob + trailing-integer extraction, copies plugin bundle into the
  detected `Plug-Ins/Effects/`, re-codesigns ad-hoc with
  `codesign --force --sign - --timestamp=none`, writes `VERSION` file.
  Logs to `/Library/Logs/CorridorKey-install.log`.
- `installer/macos/requirements.txt` — deps the postinstall pip-installs:
  corridorkey-mlx (git), numpy, Pillow, opencv-python-headless, msgpack.
- `plugin/src/CorridorKeyAE_Bridge.cpp` — `FindRepoRoot()` now includes
  `/Library/Application Support/CorridorKey` as a macOS candidate so
  the bridge finds the runtime delivered by the `.pkg`.
- CI: `installer-macos` job in `.github/workflows/ci.yml` runs on
  `macos-latest`, depends on `plugin-build`, downloads the
  `CorridorKey-macOS` artifact (which is a zip of the `.plugin` bundle —
  `actions/upload-artifact@v4` flattens single-dir parents, so we
  zip-wrap before upload), unpacks into `build/plugin/`, runs
  `build_macos.sh`, verifies via `pkgutil --expand`, uploads
  `CorridorKey-Installer-macOS` artifact.
- `scripts/installer/clean_and_test_macos.sh` — one-shot clean-slate
  harness. Wipes plugin copies from every AE install, nukes
  `/Library/Application Support/CorridorKey`, user-level caches,
  `pkgutil --forget com.corridorkey.ae`, stray runtime processes,
  port handoff file. Then optionally installs a `.pkg` and verifies
  (install tree layout + AE plugin drop + venv import check). Usage:
  `./scripts/installer/clean_and_test_macos.sh --from-ci` to test the
  latest CI-built installer.
- README: Gatekeeper unblock documented (System Settings → Privacy &
  Security → Open Anyway, or `xattr -d com.apple.quarantine`).
- **Verified:** user ran clean-slate cleanup, double-clicked CI-built
  `.pkg`, installed successfully, launched AE, CorridorKey appeared in
  Effect ▸ Keying ▸ CorridorKey, keying worked end-to-end.

### Key macOS learnings that transfer to Windows
1. **Don't pre-build the venv on the CI runner.** Venvs bake absolute
   paths into `pyvenv.cfg` and shebangs. Create the venv at install
   time on the user's machine instead. The installer payload ships
   Python + requirements.txt; postinstall runs `python -m venv` + pip.
   Install is slower (~1 minute) but bulletproof across machines.
2. **Ship runtime source + requirements.txt, not a prebuilt venv.**
   This keeps the installer small and avoids the abspath bake-in.
3. **The `CorridorKey-macOS` artifact from the plugin-build job is
   required input for the installer-macos job.** Same pattern should
   work for Windows: `installer-windows` needs `needs: plugin-build`
   and downloads `CorridorKey-Windows` (which is the raw `.aex`
   already — no bundle wrapper to preserve on Windows).
4. **AE version detection: glob for `Adobe After Effects *`, extract
   trailing integer, pick highest.** The same bash logic translates
   directly to Pascal in InnoSetup's `[Code]` section.

### Done (Windows)
- `scripts/installer/build_windows.ps1` — orchestrator, mirror of
  `build_macos.sh`. Downloads `python-build-standalone` 3.12.13
  `x86_64-pc-windows-msvc` into `.build-cache/`, stages the tree
  (embedded Python + runtime source + plugin .aex + requirements +
  VERSION) at `build/installer-windows/staging/`, invokes
  `ISCC.exe` with `/DAppVersion`, `/DStagingDir`, `/DPluginAex`, drops
  a ~27 MB `CorridorKey-<ver>-windows-x64.exe` in `dist/`. Uses the
  explicit Windows `tar.exe` at `$env:SystemRoot\System32\tar.exe`
  because Git Bash / MSYS ship a GNU tar on `PATH` that tries to
  parse `C:\...` as a remote host and fails with `Cannot connect
  to C`.
- `installer/windows/CorridorKey.iss` — InnoSetup 6 script.
  `PrivilegesRequired=admin` (Plugin Loading.log confirms AE Windows
  only scans `C:\Program Files\Adobe\Adobe After Effects *\Support
  Files\...` — no per-user Plug-ins folder gets scanned, so a no-UAC
  install isn't possible). `DefaultDirName={autopf}\CorridorKey`
  (= `C:\Program Files\CorridorKey\`). `[Files]` bundles staged tree,
  `[Run]` creates the venv and pip-installs base deps + CUDA torch
  at install time (torch is the slow step, ~5 min over 50 Mbps
  because of the 2 GB cu121 wheels). `[Code]`
  `FindHighestAEPluginsDir` mirrors the bash idiom from the macOS
  postinstall (enumerate `Program Files\Adobe\Adobe After Effects *`,
  extract trailing integer, pick highest) and `CopyFile`s the .aex in
  during `ssPostInstall`. `RemovePluginFromAllAEInstalls` runs on
  uninstall. `[UninstallDelete]` nukes the install-time-created .venv
  + the install root (InnoSetup doesn't track files created by
  `[Run]` steps, so we explicitly tell it what to remove).
  AppId `{{B3D9C2A7-E5F1-4A8B-9C2D-CORRIDORKEY01}}` pinned for
  upgrade-in-place recognition.
- `installer/windows/requirements.txt` — base deps: msgpack, numpy,
  Pillow, opencv-python-headless, timm, safetensors.
- `installer/windows/requirements-torch.txt` — pinned CUDA wheels
  (`torch==2.5.1+cu121`, `torchvision==0.20.1+cu121`) with
  `--index-url https://download.pytorch.org/whl/cu121` inside the
  file so pip uses ONLY that index for these two packages.
- `plugin/src/CorridorKeyAE_Bridge.cpp` — `FindRepoRoot()` already
  checked `%ProgramFiles%\CorridorKey` before the installer work,
  so no bridge changes were needed on the Windows side.
- CI: `installer-windows` job in `.github/workflows/ci.yml` runs on
  `windows-latest`, `needs: plugin-build`, downloads the
  `CorridorKey-Windows` artifact (raw `.aex`, no zip-wrap needed
  because `.aex` is a single file not a bundle), installs InnoSetup
  6 via pre-installed Chocolatey, runs `build_windows.ps1`, verifies
  the output is a valid PE (MZ header) ≥ 1 MB, uploads
  `CorridorKey-Installer-Windows` artifact. Build takes ~100 s —
  fast because all of the slow stuff (PyTorch CUDA wheel downloads)
  happens at install time on the user's machine, not at build time.
- `scripts/installer/clean_and_test_windows.ps1` — mirror of
  `clean_and_test_macos.sh`. Bails if AE is running or the shell
  isn't elevated. Kills stray runtime processes, removes `.aex`
  from every AE install, runs any existing `unins000.exe` then
  force-removes `%ProgramFiles%\CorridorKey\`, cleans the Phase 4
  dev tree at `%LOCALAPPDATA%\CorridorKey\` while preserving the
  models cache by default (`-KeepModelCache` switch), cleans temp
  files + orphaned InnoSetup registry receipts, warns about
  `CORRIDORKEY_REPO_ROOT` user env var. Installs via
  `-ExePath`, `-FromCi` / `-RunId`, or falls back to the latest
  local build in `dist/`. Verifies the install tree + plugin drop
  + venv imports including `torch.cuda.is_available()`.
- **Verified:** user ran clean-and-test harness, installer picked
  up the local build, placed `%ProgramFiles%\CorridorKey\` with
  the venv + cu121 torch + CUDA import check passing, dropped
  `CorridorKey.aex` into AE 2026, keying worked end-to-end.
  CI first-run on `feature/installers`: 7/7 jobs green.

### Key Windows learnings (on top of the shared macOS ones)
1. **AE on Windows doesn't scan a per-user plug-ins folder.**
   `%AppData%\Adobe\After Effects\<ver>\Plug-ins\` is NOT one of the
   paths AE scans at startup — confirmed by reading
   `%AppData%\Adobe\After Effects\26.0\Plugin Loading.log`. Only
   `C:\Program Files\Adobe\Adobe After Effects *\Support Files\...`
   paths get scanned. This means `PrivilegesRequired=admin` is
   unavoidable on Windows just as `localSystem` is on macOS.
2. **Don't pre-bake the venv on the CI runner.** Same lesson as
   macOS: venvs bake absolute paths into `pyvenv.cfg` and launchers,
   which break the moment the installer leaves the build machine.
   Install time venv creation is the bulletproof pattern.
3. **Use the explicit Windows `tar.exe` by full path** in the
   build script. PowerShell's `tar.exe` resolves via `PATH`, which
   on a dev machine with Git Bash / MSYS installed picks up GNU
   tar. GNU tar interprets `C:\path` as a remote `host:path` and
   fails with `Cannot connect to C`. The Win10+ bundled BSD tar
   lives at `$env:SystemRoot\System32\tar.exe` and handles
   `.tar.gz` + Windows paths natively.
4. **The Windows plugin-build artifact doesn't need zip-wrapping**
   the way macOS does — `.aex` is a single file, not a directory
   bundle, so `actions/upload-artifact`'s single-dir-parent
   flattening isn't a problem.
5. **InnoSetup's `[Run]` steps are NOT tracked for uninstall.**
   The venv we create via `python -m venv` at install time doesn't
   get auto-removed by the uninstaller. Have to explicitly list
   `Type: filesandordirs; Name: "{app}\runtime\.venv"` under
   `[UninstallDelete]` so it gets nuked on uninstall.
6. **InnoSetup renamed `FileCopy` → `CopyFile`** in a recent
   version. Using `FileCopy` still compiles but emits a
   `[Hint]` warning. Use `CopyFile` directly.
7. **Windows PowerShell 5 (pre-Core) is picky about Unicode in
   scripts without a UTF-8 BOM.** Em-dashes and smart quotes in
   `Write-Host` strings will throw `Missing closing ')'` parse
   errors. Keep `.ps1` files ASCII-only or save them with a BOM.

### Architectural non-goals for installers (don't re-litigate)
- **Signing / notarization** — deferred. Needs an Apple Developer ID
  ($99/yr) and an EV code-signing cert for Windows ($100–300/yr).
  Documented Gatekeeper/SmartScreen workarounds in README.
- **Bundled model weights** — deferred. They auto-download on first
  frame from the upstream GitHub release (~398 MB one-time) and the
  UI already shows "Loading model..." status. Bundling would 10x
  the installer size.
- **Upgrade migrations** — for now, the clean path is uninstall old
  then install new. Revisit if upgrade-in-place becomes a real
  complaint.

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
  Fastest ~187 ms, Fast ~230 ms, High ~286 ms, Full Res ~612 ms.
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
- Multi-Frame Rendering (threaded, serialized via bridge mutex)
- Auto-launch runtime subprocess (fork/exec on macOS, CreateProcessW on
  Windows; port discovery via temp file)
- Background engine loading: IPC server binds immediately, engine loads
  in a background thread, handler returns `LOADING` responses in the
  meantime. The render path blocks on the runtime's ready state instead
  of caching pass-through frames as the final result.
- Reconnect with exponential backoff cooldown
- Clean process-tree shutdown on Windows via a Job Object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` — terminates the venv launcher
  and the real python child atomically, no orphans. Runtime also
  honours the graceful JSON shutdown message via a delayed
  `os._exit(0)` from a daemon thread so the ack flushes first.
- Custom Drawbot UI: logo, title, tagline, clickable About link, live
  status line (Ready / Loading model / Bridge error)
- Alpha hint layer input (PF_ADD_LAYER)
- Output modes: Processed, Matte, Foreground, Composite
- Effect params: Output Mode, Alpha Hint, Quality, Despill, Despeckle, Refiner, Matte Cleanup
- Tiled inference for full-resolution output (MLX) / native 2048 per
  quality size (PyTorch)
- Debug image saves gated behind CK_DEBUG=1 env var

## Architectural non-goals

Two things we explicitly will NOT build, documented so we don't
re-litigate them every few weeks:

- **True async / non-blocking render (#6, closed).** AE's effect render
  model is synchronous — `PF_Cmd_SMART_RENDER` must return pixels in
  that call, there is no "I'll get back to you" callback, and there's
  no public API for an effect to invalidate its own cached frame from
  a background thread. Returning a placeholder poisons AE's
  `(time, params)` cache until the user wiggles a parameter. What we
  DO have (Smart Render + MFR + background engine loading + two-tier
  frame cache) handles the interactive cases that matter. Use the
  AEGP API only if we end up needing a companion general plug-in for
  other reasons.
- **Parallel MFR inference (#12, closed).** The GPU serializes at the
  driver level — two concurrent MLX/CUDA forward passes on the same
  model don't actually overlap, they queue. A connection pool in the
  bridge would give us ~15–25% steady-state throughput by overlapping
  CPU pre/post work with GPU inference, but the implementation cost
  (thread-safe runtime handler, engine-level locking, pool bookkeeping)
  is not worth it at current per-frame speeds. Re-open only if a batch
  export workflow becomes a real complaint.

## Quality gates
- Python runtime: `ruff check` clean, `mypy --config-file pyproject.toml`
  (strict + warn_return_any) clean, 23/23 tests passing under
  `pytest tests/`. Tests are CI-ready: no network, no GPU, no real
  weights required.
- C++ plugin: MSVC `/W4` clean on Windows with two narrow silences
  (`/wd4100` for AE-SDK callback unref-params, `/wd4201` for
  SDK-header nameless unions). macOS uses Xcode defaults.

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
