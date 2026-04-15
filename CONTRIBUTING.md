# Contributing to CorridorKey AE

Thanks for your interest. CorridorKey AE is a small project run in the
open, and PRs, bug reports, and feature requests are all welcome.

This document covers everything you need to build from source, run the
test suite, and open a PR. If you just want to *use* the plugin, head
back to the [README](README.md) — the installer is the friction-free
path.

## Table of contents

- [Project layout](#project-layout)
- [Prerequisites](#prerequisites)
- [Building on macOS](#building-on-macos)
- [Building on Windows](#building-on-windows)
- [Running the tests](#running-the-tests)
- [Architecture](#architecture)
- [Code style](#code-style)
- [Submitting a PR](#submitting-a-pr)
- [Cutting a release](#cutting-a-release)

## Project layout

```
corridorkey-ae/
├── plugin/                 # C++ AE effect plugin (CMake)
│   ├── src/                # Entry point, params, render, bridge, custom UI
│   ├── include/            # Headers
│   └── resources/          # PiPL, Info.plist, logo
├── runtime/                # Python inference backend
│   ├── server/             # IPC server, handler, hardware detection
│   ├── engines/            # MLX engine (macOS), PyTorch engine (Windows)
│   ├── models/             # Model manager, download
│   └── tests/              # pytest suite (no network, no GPU needed)
├── shared/                 # Cross-layer protocol definitions
├── scripts/
│   ├── bootstrap/          # Dev helpers (run_tests.sh, etc.)
│   └── installer/          # Build + clean scripts for macOS pkg + Windows exe
├── installer/
│   ├── macos/              # pkgbuild/productbuild config + postinstall
│   └── windows/            # InnoSetup .iss + requirements
├── docs/                   # Research notes, PRD drafts
└── .github/workflows/      # CI pipeline
```

## Prerequisites

**Both platforms**
- CMake 3.15 or newer
- Adobe After Effects SDK — free, but requires registration at
  [Adobe Developer Console](https://developer.adobe.com/console/). The
  SDK is not redistributable and not in this repo.
- Python 3.10 or newer (3.12 recommended — matches what the installer ships)
- Git

**macOS-specific**
- Xcode Command Line Tools (`xcode-select --install`)
- Homebrew Python 3.12 (`brew install python@3.12`) or equivalent

**Windows-specific**
- Visual Studio Build Tools 2019 or 2022 with the **Desktop development
  with C++** workload. VS 2019 (16.x) is what CI uses.
- The AE Windows SDK ships as a `.zst` that needs
  [`extractzstd.bat`](https://github.com/facebook/zstd) to unpack.
- NVIDIA GPU with a driver that supports CUDA 12.1 (for running the plugin,
  not for building it)

## Building on macOS

```bash
git clone https://github.com/iamjoshuadavies/corridorkey-ae.git
cd corridorkey-ae

# 1. Build the plugin
cmake -B build -S . \
      -DCMAKE_BUILD_TYPE=Debug \
      -DAE_SDK_PATH=/absolute/path/to/ae_sdk
cmake --build build

# 2. Set up the Python runtime
cd runtime
python3.12 -m venv .venv
source .venv/bin/activate
pip install -e ".[mlx,dev]"
cd ..

# 3. Symlink the plugin bundle into AE so rebuilds auto-update
sudo ln -s "$(pwd)/build/plugin/CorridorKey.plugin" \
  "/Applications/Adobe After Effects 2026/Plug-ins/Effects/CorridorKey.plugin"
```

Clean rebuild (needed when PiPL flags or out_flags change — AE caches
aggressively):

```bash
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/path/to/ae_sdk
cmake --build build
```

The bridge auto-discovers the Python runtime by walking up from the
plugin bundle and also checking `/Library/Application Support/CorridorKey`
(the installer's drop location). For a source checkout, the walk-up
works out of the box — no env var needed.

## Building on Windows

```powershell
git clone https://github.com/iamjoshuadavies/corridorkey-ae.git
cd corridorkey-ae

# 1. Build the plugin
cmake -B build_win -S . -G "Visual Studio 16 2019" -A x64 `
      -DAE_SDK_PATH="C:/absolute/path/to/AfterEffectsSDK/Examples"
cmake --build build_win --config Release

# 2. Set up the Python runtime (PyTorch + CUDA + deps)
cd runtime
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install msgpack numpy Pillow opencv-python-headless timm safetensors
pip install torch==2.5.1+cu121 torchvision==0.20.1+cu121 `
  --index-url https://download.pytorch.org/whl/cu121
cd ..

# 3. Install the .aex into AE (admin shell required; close AE first)
Copy-Item -Force .\build_win\plugin\Release\CorridorKey.aex `
  "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Effects\CorridorKey.aex"

# 4. Point the bridge at your source checkout (once, user-level env var)
[Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', `
  (Resolve-Path .).Path, 'User')
```

After setting `CORRIDORKEY_REPO_ROOT`, relaunch AE — env vars are
inherited at process start, so a running AE won't pick up the change.

The bridge auto-discovers the Python runtime in this order:

1. `CORRIDORKEY_REPO_ROOT` env var (dev escape hatch)
2. `%LOCALAPPDATA%\CorridorKey\runtime\.venv\Scripts\python.exe` (per-user install)
3. `%ProgramFiles%\CorridorKey\runtime\.venv\Scripts\python.exe` (system-wide install)

## Running the tests

The Python runtime has a pytest suite that runs with no network, no GPU,
and no real model weights — it uses a mock engine so it's safe in CI.

```bash
# macOS / Linux
cd runtime
source .venv/bin/activate
python -m pytest tests/ -v

# Windows
cd runtime
.\.venv\Scripts\Activate.ps1
python -m pytest tests/ -v
```

Quality gates the CI enforces:

- `ruff check runtime/` — clean
- `mypy --config-file runtime/pyproject.toml runtime/` — clean (strict + warn_return_any)
- `pytest runtime/tests/` — 23/23 passing
- MSVC `/W4` clean on Windows (two narrow silences: `/wd4100` for
  AE-SDK callback unref-params, `/wd4201` for SDK-header nameless
  unions)

Before opening a PR, run the full suite locally:

```bash
scripts/bootstrap/run_tests.sh   # macOS/Linux
```

## Architecture

Three layers, cleanly separated:

- **Plugin (`plugin/`)** — C++ After Effects effect. Registers params,
  handles `PF_Cmd_SMART_RENDER` + Multi-Frame Rendering (serialized via
  a bridge mutex), runs the custom Drawbot UI (logo, tagline, status
  line, clickable About), and owns the socket to the runtime.
- **Bridge (`plugin/src/CorridorKeyAE_Bridge.cpp`)** — discovers and
  launches the Python runtime subprocess, reads the port handoff file
  at `<temp>/corridorkey_runtime.port`, establishes a TCP connection to
  `127.0.0.1:<port>`, and provides the C++ plugin a blocking `Process`
  call that converts frames to the IPC wire format and back.
- **Runtime (`runtime/`)** — Python inference service. Binds the IPC
  server immediately on startup, loads the engine in a background
  thread, and responds with `LOADING` until the engine is ready. MLX
  engine on macOS, PyTorch engine on Windows.

### IPC protocol

Length-prefixed binary messages over TCP on `127.0.0.1`. The runtime
picks an ephemeral port at bind time and writes `<pid> <port>\n` to
`<temp>/corridorkey_runtime.port` so the bridge can find it.

- **Control messages** — JSON text: ping, status, shutdown
- **Frame messages** — binary: `"FRAME"` magic + dimensions + params +
  ARGB 8bpc pixel data + optional alpha hint
- **Response** — `"FRAME"` magic + dimensions + processed ARGB 8bpc

All pixel data is ARGB 8bpc on the wire. The C++ side converts from the
project's working bit depth (8/16/32bpc float) to uint8 before sending
and back on receive.

### Inference pipeline

**macOS:** MLX engine via the `corridorkey_mlx` package (pip from
GitHub). Tiled inference (tile_size=512, overlap=64). Weights cache at
`~/Library/Application Support/CorridorKey/models/` and auto-download
from the upstream corridorkey-mlx GitHub release on first run.

**Windows:** PyTorch engine. Self-contained — uses a vendored
`GreenFormer` (`runtime/engines/_greenformer.py`) and a weights loader
(`runtime/engines/_weights_loader.py`) that auto-downloads the official
MLX safetensors from the corridorkey-mlx GitHub release and applies the
inverse of upstream's PyTorch→MLX converter (transpose conv kernels
NCHW↔NHWC + rename refiner stem keys) to load into the vendored PyTorch
model. Multi-resolution model cache wired to the Quality dropdown:
Fastest=512 no refiner, Fast=512, High=1024, Full Res=2048. Cache lives
at `%LOCALAPPDATA%\CorridorKey\models\`.

Both engines apply ImageNet normalization to RGB inputs (mean `[0.485,
0.456, 0.406]`, std `[0.229, 0.224, 0.225]`). Skipping this is what
produces washed-out "milky" foreground output — the Hiera backbone needs
the normalized distribution. The alpha-hint channel is NOT normalized
(it's a mask, not color data).

### Architectural non-goals

Two things we explicitly will NOT build, documented so we don't
re-litigate them every few weeks:

- **True async / non-blocking render ([#6](https://github.com/iamjoshuadavies/corridorkey-ae/issues/6), closed).**
  AE's effect render model is synchronous — `PF_Cmd_SMART_RENDER` must
  return pixels in that call. There is no public API for an effect to
  invalidate its own cached frame from a background thread. Returning a
  placeholder poisons AE's `(time, params)` cache until the user wiggles
  a parameter. What we have (Smart Render + MFR + background engine
  loading + two-tier frame cache) handles the interactive cases that
  matter.
- **Parallel MFR inference ([#12](https://github.com/iamjoshuadavies/corridorkey-ae/issues/12), closed).**
  The GPU serializes at the driver level — two concurrent MLX/CUDA
  forward passes on the same model don't actually overlap, they queue.
  A connection pool in the bridge would give us ~15–25% steady-state
  throughput by overlapping CPU pre/post work with GPU inference, but
  the implementation cost isn't worth it at current per-frame speeds.
  Re-open only if a batch export workflow becomes a real complaint.

## Code style

- **C++** — modern C++17, CMake, minimal dependencies. Match the
  surrounding style (4-space indent, braces on same line for functions,
  `k`-prefixed constants). MSVC `/W4` clean on Windows.
- **Python** — `ruff check` clean, `mypy` strict clean. 4-space indent,
  type hints on all public functions, docstrings where non-obvious.
- **Commit messages** — imperative mood (*"Add foo"*, not *"Added foo"*
  or *"Adds foo"*). First line under 72 chars. Reference issues with
  `#NNN` when relevant.

## Submitting a PR

1. **Fork** the repo and create a feature branch:
   `git checkout -b feature/my-thing`.
2. **Make your change.** Keep the diff focused — one logical change per
   PR. If you're fixing two unrelated bugs, that's two PRs.
3. **Run the tests** and the linters locally. CI will catch it if you
   don't, but local is faster.
4. **Open a PR** with a clear title and a description that covers: what
   you changed, why, and how you tested it.
5. **CI must be green** before merge. Both platform builds, both
   installer builds, and all three Python versions run on every PR.

Small PRs get reviewed fast. Big PRs get reviewed slowly (or not at all
if the scope creeps into "rewrite everything"). If you're planning a
large change, open an issue first to discuss the approach.

## Cutting a release

Only project maintainers cut releases, but the mechanism is documented
here for transparency.

```bash
# From main, at the commit you want to ship:
git checkout main
git pull
git tag v0.2.0
git push origin v0.2.0
```

Pushing a `v*` tag triggers the full CI pipeline plus the `release` job,
which downloads both installer artifacts, generates SHA-256 checksums,
and publishes a GitHub Release with the version baked into the artifact
filenames. Prerelease tags (`v0.2.0-rc1`, `v0.2.0-alpha1`,
`v0.2.0-beta2`) are auto-flagged as prereleases.

If a tag build goes wrong and you need to re-cut:

```bash
git tag -d v0.2.0
git push --delete origin v0.2.0
gh release delete v0.2.0 --yes
# fix the problem, then re-tag and push
```

## Questions?

Open an [issue](https://github.com/iamjoshuadavies/corridorkey-ae/issues/new/choose)
or a [discussion](https://github.com/iamjoshuadavies/corridorkey-ae/discussions)
(if enabled). For security-sensitive reports, see [SECURITY.md](SECURITY.md).
