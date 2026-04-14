<p align="center">
  <img src="plugin/resources/logo_256.png" alt="CorridorKey" width="128" height="128">
</p>

<h1 align="center">CorridorKey AE</h1>

<p align="center">
  Native Adobe After Effects plugin for advanced green-screen keying.<br>
  Based on the technique created by <strong>Niko Pueringer</strong> of <a href="https://youtube.com/CorridorCrew">Corridor Digital</a>.
</p>

---

> **Status:** Active development.
> - **macOS (Apple Silicon):** keying pipeline working end-to-end via MLX.
> - **Windows (x64):** plugin builds, loads in AE, and the IPC bridge auto-launches the runtime. Inference engine still to come (see Remaining Work).

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
│   Python Runtime         │  Tiled inference + postprocessing
│   MLX (Mac) / WIP (Win)  │  Auto-launched as a subprocess on first frame
└──────────────────────────┘
```

## Performance

| Quality Mode | Resolution | Speed (M1/M2) | Use Case |
|-------------|-----------|---------------|----------|
| Fastest (256) | Downscale to 256 | ~0.1s | Scrubbing, quick preview |
| Fast (512) | Downscale to 512 | ~0.3s | Interactive work |
| High (1024) | Downscale to 1024 | ~1-2s | Higher quality preview |
| Full Res (Tiled) | Native resolution | ~4.6s at 1080p | Final render, production |

Cached frames return instantly. Changing post-processing sliders (despill, despeckle, cleanup) skips model inference and re-applies only the cheap post-processing (~10ms).

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

# Runtime — minimal deps (no MLX on Windows yet)
cd runtime
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install msgpack numpy Pillow opencv-python-headless

# Install the .aex into AE's plug-ins folder (admin shell required —
# Program Files is protected). Close AE first; the file is locked while open.
Copy-Item -Force .\build_win\plugin\Release\CorridorKey.aex `
  "C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Effects\CorridorKey.aex"

# Tell the bridge where the runtime venv lives. Required on Windows because
# the .aex sits in Program Files with no path-relationship to the source repo.
[Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', 'C:\path\to\corridorkey-ae', 'User')
```

Then launch After Effects fresh (env vars are inherited at process start).
The plugin appears under **Effect → Keying → CorridorKey**, auto-launches
the runtime on first render, and renders the "no model" fallback overlay.

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

- [ ] **Windows inference engine** — PyTorch/CUDA backend (the EZ-CorridorKey
      Windows app ships a `.pth` we can target). Plugin + runtime IPC already
      work on Windows; only the engine is missing.
- [ ] **Parallel MFR inference** (#12) — connection pool for multi-threaded rendering
- [ ] **Float32 pipeline** (#10) — skip uint8 quantization for 32bpc projects
- [ ] **Async render** (#6) — non-blocking inference for smoother UI
- [ ] **Packaging** (M6) — installer, signing, notarization, GitHub Releases

## Credits

Based on the green-screen keying technique created by **Niko Pueringer** of [Corridor Digital](https://youtube.com/CorridorCrew). Model inference via [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx).

## License

[PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) — see [LICENSE](LICENSE).

- ✅ **Use commercially** — use CorridorKey AE on paid VFX/film/video projects
- ✅ **View & modify source** — fork it, improve it, contribute back
- ✅ **Share freely** — distribute to other creatives
- ❌ **Don't sell the tool** — can't resell or build a competing commercial product from it

The upstream [CorridorKey](https://github.com/nikopueringer/CorridorKey) is licensed under CC BY-NC-SA 4.0.
