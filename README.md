<p align="center">
  <img src="plugin/resources/logo_256.png" alt="CorridorKey" width="128" height="128">
</p>

<h1 align="center">CorridorKey AE</h1>

<p align="center">
  Native Adobe After Effects plugin for advanced green-screen keying.<br>
  Based on the technique created by <strong>Niko Pueringer</strong> of <a href="https://youtube.com/CorridorCrew">Corridor Digital</a>.
</p>

---

> **Status:** Active development. Keying pipeline working end-to-end on both
> platforms — both bootstrap from zero on first run with no extra setup.
> - **macOS (Apple Silicon):** MLX inference, ~4.8s/frame at 1080p (tiled)
>   on a MacBook Pro M5.
> - **Windows (x64):** PyTorch CUDA inference. Quality dropdown switches
>   between 512/1024/2048 model sizes; ~190 ms at Fastest, ~610 ms at Full
>   Res on an RTX 4090.
>
> Both platforms download the model weights on first run from the
> [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx)
> GitHub release (~398 MB, one-time, cached locally). The Windows path
> applies a reverse MLX→PyTorch conversion to load the downloaded
> safetensors into a vendored GreenFormer.

## What It Does

CorridorKey AE brings physically accurate green-screen separation directly into After Effects. Apply the effect, point it at an alpha hint layer, and get production-quality keying with foreground extraction, despill, and matte cleanup — all powered by ML inference running locally on your machine.

**Key features:**
- Cross-platform: macOS (Apple Silicon) and Windows (x64)
- Real-time green-screen keying via MLX on Apple Silicon
- Tiled inference for full native resolution output (1080p, 4K)
- Multiple output modes: Processed, Matte, Foreground, Composite
- External alpha hint layer input (precomp a rough key, feed it in)
- Post-processing: Despill, Despeckle, Matte Cleanup, Refiner
- Quality presets: Fastest (256) → Fast (512) → High (1024) → Full Res (Tiled)
- Smart Render with 8/16/32bpc float support
- Multi-Frame Rendering (MFR) enabled
- Two-tier frame caching (raw model output + post-processed response)
- Auto-launch runtime subprocess — no manual server start needed
- Auto-download model weights on first run
- Custom branded UI with logo, tagline, and About dialog

## Architecture

```
┌──────────────────────────┐
│   After Effects Host     │
│   CorridorKey Plugin     │  C++ native effect, Drawbot UI, Smart Render
│   (.plugin / .aex)       │  macOS bundle on Mac, .aex DLL on Windows
└──────────┬───────────────┘
           │ TCP socket on 127.0.0.1 (length-prefixed binary)
           │ Port handed off via temp-file
┌──────────▼───────────────┐
│   Python Runtime         │  Inference + postprocessing
│   MLX (Mac) / Torch (Win)│  Auto-launched as a subprocess on first frame
└──────────────────────────┘
```

## Performance

**macOS (MacBook Pro M5 via MLX)** — tiled inference, working resolution per preset:

| Quality Mode | Resolution | Speed (1080p input) | Use Case |
|---|---|---|---|
| Fastest (256) | Downscale to 256 | ~460 ms | Scrubbing, quick preview |
| Fast (512) | Downscale to 512 | ~460 ms | Interactive work |
| High (1024) | Downscale to 1024 | ~760 ms | Higher quality preview |
| Full Res (Tiled) | Native resolution | ~4.8 s | Final render, production |

(Fastest and Fast are effectively the same on M5 — at these sizes the model
runs at the same effective rate, so there's little benefit to 256 over 512.)

**Windows (RTX 4090 via PyTorch CUDA, fp16)** — each preset switches to a
different model size (pos_embed bicubic-interpolated at load time):

| Quality Mode | Model | Speed (1080p input) |
|---|---|---|
| Fastest (256) | 512 no refiner | ~187 ms |
| Fast (512) | 512 + refiner | ~230 ms |
| High (1024) | 1024 + refiner | ~286 ms |
| Full Res | 2048 + refiner | ~612 ms |

All three model sizes stay live on the GPU together (~0.5 GB total VRAM)
so switching Quality doesn't trigger a reload. Cached frames return
instantly. Changing post-processing sliders (despill, despeckle, cleanup)
skips model inference and re-applies only the cheap post-processing
(~10 ms).

**First run on a fresh install** takes longer: the runtime downloads
~398 MB of weights from the upstream GitHub release, builds the model
on the GPU, and warms CUDA kernels. The effect panel shows
**"Loading model (...)"** status during the wait; AE stays responsive.
Every subsequent launch uses the cached weights and is near-instant.

## Building

CorridorKey AE builds from one CMake project on both macOS and Windows.

### Common prerequisites
- CMake 3.15+
- Adobe After Effects SDK (place in `ae_sdk/` or set `AE_SDK_PATH`)
- Python 3.10+

### macOS (Apple Silicon)

Toolchain: Xcode Command Line Tools, Python 3.12 via Homebrew.

```bash
# Plugin
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/path/to/ae_sdk
cmake --build build

# Runtime
cd runtime
python3.12 -m venv .venv
source .venv/bin/activate
pip install -e ".[mlx,dev]"

# Symlink the plugin into AE for live development
sudo ln -s $(pwd)/../build/plugin/CorridorKey.plugin \
  "/Applications/Adobe After Effects 2026/Plug-ins/Effects/CorridorKey.plugin"
```

### Windows (x64)

Toolchain: Visual Studio Build Tools 2019 or 2022 with the **Desktop development
with C++** workload, plus standalone CMake (or the one bundled with VS).
Extract the AE Windows SDK with the bundled `extractzstd.bat`.

```powershell
# Plugin — use the VS generator that matches your installed Build Tools.
# Pass the Examples folder of the unpacked SDK as AE_SDK_PATH.
cmake -B build_win -S . -G "Visual Studio 16 2019" -A x64 `
      -DAE_SDK_PATH="C:\path\to\AfterEffectsSDK\Examples"
cmake --build build_win --config Release

# Runtime — install minimal IPC deps + PyTorch CUDA + the model code's deps
cd runtime
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install msgpack numpy Pillow opencv-python-headless timm safetensors
pip install torch==2.5.1+cu121 torchvision==0.20.1+cu121 --index-url https://download.pytorch.org/whl/cu121

# That's it — the runtime auto-downloads the model weights (~398 MB) from
# the corridorkey-mlx GitHub release on first frame and caches them under
# %LOCALAPPDATA%\CorridorKey\models\.

# Install the .aex into AE's plug-ins folder (admin shell required —
# Program Files is protected). Close AE first; the file is locked while open.
Copy-Item -Force .\build_win\plugin\Release\CorridorKey.aex `
  "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Effects\CorridorKey.aex"
```

The bridge auto-discovers the Python runtime in this order:
1. `CORRIDORKEY_REPO_ROOT` env var (dev escape hatch — point at a source checkout)
2. `%LOCALAPPDATA%\CorridorKey\runtime\.venv\Scripts\python.exe` (per-user install)
3. `%ProgramFiles%\CorridorKey\runtime\.venv\Scripts\python.exe` (system-wide install)

For dev work from a source checkout, set the env var once:
```powershell
[Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', 'C:\path\to\corridorkey-ae', 'User')
```

Then launch After Effects fresh (env vars are inherited at process start).
The plugin appears under **Effect → Keying → CorridorKey**, auto-launches
the runtime on first render, and downloads the model weights on first
frame (~398 MB, one-time).

### Tests

```bash
cd runtime && python -m pytest tests/ -v
```

## Project Structure
```
corridorkey-ae/
├── plugin/                 # C++ AE effect plugin
│   ├── src/                # Source: entry point, params, render, bridge, UI
│   ├── include/            # Headers
│   └── resources/          # PiPL, Info.plist, logo
├── runtime/                # Python inference backend
│   ├── server/             # IPC server, handler, hardware detection
│   ├── engines/            # MLX engine, postprocessing
│   ├── models/             # Model manager, download
│   └── tests/              # Test suite
├── shared/                 # Cross-layer protocol definitions
├── scripts/                # Build, package, release automation
├── docs/                   # PRD, research, architecture
└── .github/workflows/      # CI pipeline
```

## Effect Controls

| Control | Description |
|---------|-------------|
| **Output Mode** | Processed / Matte / Foreground / Composite |
| **Alpha Hint** | Layer input for external alpha matte |
| **Quality** | Fastest (256) → Full Res (Tiled) |
| **Despill** | Remove green spill from edges (0-1) |
| **Despeckle** | Remove small matte noise (0-1) |
| **Refiner** | Edge refinement strength (0-1) |
| **Matte Cleanup** | Tighten and smooth matte edges (0-1) |

## Remaining Work

See [open issues](https://github.com/iamjoshuadavies/corridorkey-ae/issues) for the full backlog. Key items:

- [ ] **Async render** (#6) — non-blocking inference so AE stays
      responsive during normal playback / scrubbing (first-run UX is
      already handled — the engine loads in a background thread and
      the render path shows a loading status instead of freezing).
- [ ] **Parallel MFR inference** (#12) — connection pool for multi-threaded rendering
- [ ] **Float32 pipeline** (#10) — skip uint8 quantization for 32bpc projects
- [ ] **macOS regression retest** (#23) — after the Windows port refactor
- [ ] **Windows codesigning + installer** (#24) — MSI or NSIS, bundle
      the runtime next to the `.aex`, drop the dev env var requirement
- [ ] **CI** (#26) — GitHub Actions matrix build for macOS + Windows

## Credits

Based on the green-screen keying technique created by **Niko Pueringer** of [Corridor Digital](https://youtube.com/CorridorCrew). Model inference via [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx).

## License

[PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) — see [LICENSE](LICENSE).

- ✅ **Use commercially** — use CorridorKey AE on paid VFX/film/video projects
- ✅ **View & modify source** — fork it, improve it, contribute back
- ✅ **Share freely** — distribute to other creatives
- ❌ **Don't sell the tool** — can't resell or build a competing commercial product from it

The upstream [CorridorKey](https://github.com/nikopueringer/CorridorKey) is licensed under CC BY-NC-SA 4.0.
