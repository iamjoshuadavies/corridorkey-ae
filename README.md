# CorridorKey AE

Native Adobe After Effects plugin for advanced green-screen keying, inspired by [CorridorKey](https://github.com/Corridor-Digital/corridorkey).

> **Status:** Early development (Milestone 1 — Native AE Effect Shell)

## What Is This?

CorridorKey AE brings physically accurate green-screen separation directly into After Effects. Apply the effect to footage, choose output modes, and render — no external apps required.

**Key features:**
- Automatic green-screen separation and keying
- Multiple output modes (Processed, Matte, Foreground, Composite)
- External alpha-hint pathway
- Low-VRAM / low-memory operation
- Hardware-aware detection and diagnostics
- First-run setup with automatic model downloads

## Architecture

```
┌─────────────────────┐
│   After Effects      │
│   Host Plugin (C++)  │  ← Thin native effect, parameter UI, frame I/O
└─────────┬───────────┘
          │ IPC (local socket, msgpack)
┌─────────▼───────────┐
│   Runtime (Python)   │  ← Model inference, hardware detection, downloads
│   PyTorch / MLX      │
└─────────────────────┘
```

## Building

### Prerequisites
- CMake 3.15+
- Adobe After Effects SDK (place in `ae_sdk/` or set `AE_SDK_PATH`)
- Xcode (macOS) or Visual Studio 2019+ (Windows)
- Python 3.10+ (for runtime)

### Plugin (C++)
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Runtime (Python)
```bash
cd runtime
python -m venv .venv
source .venv/bin/activate  # or .venv\Scripts\activate on Windows
pip install -e ".[dev]"
```

## Project Structure
```
corridorkey-ae/
├── plugin/          # C++ AE effect plugin
│   ├── src/         # Source files
│   ├── include/     # Headers
│   └── resources/   # PiPL resources, icons
├── runtime/         # Python inference backend
│   ├── server/      # IPC server
│   ├── engines/     # Inference engines (PyTorch, MLX)
│   ├── models/      # Model management & downloads
│   └── tests/       # Backend tests
├── shared/          # Cross-layer definitions
│   ├── protocol/    # IPC message definitions
│   └── schema/      # Shared data schemas
├── scripts/         # Automation
│   ├── bootstrap/   # Dev setup scripts
│   ├── package/     # Build packaging
│   └── release/     # Release automation
└── docs/            # Documentation
```

## License

**Not yet determined.** See [LICENSE](LICENSE) for details. The upstream CorridorKey project uses CC BY-NC-SA 4.0 with additional terms. Do not assume this project is open-source until licensing is resolved.
