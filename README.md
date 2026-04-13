<p align="center">
  <img src="plugin/resources/logo_64.png" alt="CorridorKey" width="64" height="64">
</p>

<h1 align="center">CorridorKey AE</h1>

<p align="center">
  Native Adobe After Effects plugin for advanced green-screen keying.<br>
  Based on the technique created by <strong>Niko Pueringer</strong> of <a href="https://youtube.com/CorridorCrew">Corridor Digital</a>.
</p>

---

> **Status:** Active development — keying pipeline working end-to-end on macOS (Apple Silicon).

## What It Does

CorridorKey AE brings physically accurate green-screen separation directly into After Effects. Apply the effect, point it at an alpha hint layer, and get production-quality keying with foreground extraction, despill, and matte cleanup — all powered by ML inference running locally on your Mac.

**Key features:**
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
│   (.plugin / .aex)       │
└──────────┬───────────────┘
           │ TCP socket (length-prefixed binary)
┌──────────▼───────────────┐
│   Python Runtime         │  corridorkey_mlx, tiled inference, postprocessing
│   MLX on Apple Silicon   │  ~4.6s/frame at 1080p (Full Res Tiled)
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

### Prerequisites
- macOS with Apple Silicon (M1/M2/M3/M4)
- CMake 3.15+
- Adobe After Effects SDK (place in `ae_sdk/` or set `AE_SDK_PATH`)
- Xcode Command Line Tools
- Python 3.10+ (3.12 recommended via Homebrew)

### Plugin (C++)
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/path/to/ae_sdk
cmake --build build
```

### Runtime (Python)
```bash
cd runtime
python3.12 -m venv .venv
source .venv/bin/activate
pip install -e ".[mlx,dev]"
```

### Development Setup
```bash
# Symlink plugin into AE for live development
sudo ln -s $(pwd)/build/plugin/CorridorKey.plugin \
  "/Applications/Adobe After Effects 2026/Plug-ins/Effects/CorridorKey.plugin"

# Run tests
cd runtime && source .venv/bin/activate && python -m pytest tests/ -v
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

- [ ] **Windows support** (M4) — CUDA/PyTorch backend, .aex build
- [ ] **Parallel MFR inference** (#12) — connection pool for multi-threaded rendering
- [ ] **Float32 pipeline** (#10) — skip uint8 quantization for 32bpc projects
- [ ] **Async render** (#6) — non-blocking inference for smoother UI
- [ ] **Packaging** (M6) — installer, signing, notarization, GitHub Releases

## Credits

Based on the green-screen keying technique created by **Niko Pueringer** of [Corridor Digital](https://youtube.com/CorridorCrew). Model inference via [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx).

## License

**Not yet determined.** See [LICENSE](LICENSE) for details. The upstream CorridorKey project uses CC BY-NC-SA 4.0 with additional terms. Do not assume this project is open-source until licensing is resolved.
