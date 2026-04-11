# CorridorKey AE - Claude Code Context

## Project Overview
Native Adobe After Effects plugin for green-screen keying, based on CorridorKey.
Three-layer architecture: Host Plugin (C++) → Bridge (IPC) → Runtime (Python).

## Build Commands
```bash
# Configure and build plugin (macOS)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run runtime tests
cd runtime && python -m pytest tests/

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

## Licensing
NOT YET RESOLVED — see LICENSE file. Do not describe as "open source."
