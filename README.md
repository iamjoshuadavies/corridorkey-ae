<p align="center">
  <img src="plugin/resources/logo_256.png" alt="CorridorKey" width="128" height="128">
</p>

<h1 align="center">CorridorKey AE</h1>

<p align="center">
  Native Adobe After Effects plugin for advanced green-screen keying.<br>
  Based on the technique created by my friend <strong>Niko Pueringer</strong> of <a href="https://youtube.com/CorridorCrew">Corridor Digital</a>.
</p>

<p align="center">
  <a href="https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml">
    <img src="https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml/badge.svg" alt="CI">
  </a>
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

## Installation

One double-click installer per platform — both bundle the Python
runtime, create a dedicated venv, pip-install all dependencies, and
drop the plugin into After Effects automatically. The only thing that
isn't bundled is the model weights: they download on first frame
(~398 MB, one-time, cached locally), with a **Loading model (...)**
status visible in the effect panel while it runs.

### System requirements

| Platform | Minimum | Verified on |
|---|---|---|
| **macOS** | Apple Silicon (M1/M2/M3/M4/M5), macOS 11 (Big Sur) or later, After Effects 2024+ | MacBook Pro M5, AE 2026 |
| **Windows** | x64 with NVIDIA GPU (CUDA 12.1 compatible), Windows 10 1803+ or Windows 11, After Effects 2024+ | RTX 4090, Windows 11, AE 2026 |

No Python install needed, no command line, no env vars. The installer
takes care of everything.

### Downloading the installer

There are no proper GitHub Releases yet — builds come from CI. Grab the
latest green run from the [**Actions tab**](https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml)
and download whichever artifact matches your platform:

| Platform | Artifact | File |
|---|---|---|
| macOS  | `CorridorKey-Installer-macOS`    | `CorridorKey-<ver>-macOS-arm64.pkg` |
| Windows | `CorridorKey-Installer-Windows` | `CorridorKey-<ver>-windows-x64.exe` |

1. Open the linked workflow page.
2. Click the most recent run with a green checkmark next to **CI**.
3. Scroll to the **Artifacts** section at the bottom.
4. Click the platform artifact — GitHub downloads it as a `.zip`.
5. Unzip it. Inside is the actual `.pkg` / `.exe`.

(If you're not signed in to GitHub, the Artifacts section won't show —
sign in and refresh.)

Proper GitHub Releases with checksums will land when the installers
get signed + notarized.

### macOS install

The installer is **not yet signed or notarized**, so macOS Gatekeeper
refuses to open it on a plain double-click with something like *"Apple
could not verify CorridorKey is free of malware"*. Expected. Unblock
it one of two ways:

**Option A — one-off, via System Settings (recommended):**

1. Double-click the `.pkg`. Gatekeeper blocks it; click **Done**.
2. Open **System Settings → Privacy & Security**.
3. Scroll to the **Security** section. You'll see a message like
   *"CorridorKey-0.1.0-macOS-arm64.pkg was blocked to protect your Mac."*
4. Click **Open Anyway**, then authenticate with your password or Touch ID.
5. The installer reopens; click **Open** on the second Gatekeeper prompt.
6. Enter your admin password when the installer asks — the payload
   writes to `/Library/Application Support/` which is root-owned.

**Option B — one command, via Terminal:**

```bash
# Strip the quarantine xattr that Gatekeeper checks for
xattr -d com.apple.quarantine ~/Downloads/CorridorKey-*.pkg

# Then double-click as normal, or install from the terminal:
sudo installer -pkg ~/Downloads/CorridorKey-*.pkg -target /
```

Either route: once installed, the plugin runs with no further prompts.
Gatekeeper only gates the `.pkg` itself, not the postinstall steps or
the plugin at runtime.

Installs land at:
- `/Library/Application Support/CorridorKey/` — bundled Python, venv, runtime source
- `/Applications/Adobe After Effects <version>/Plug-Ins/Effects/CorridorKey.plugin` — the effect
- `/Library/Logs/CorridorKey-install.log` — installer's own log (useful if something breaks)

### Windows install

The installer is **not yet signed with an EV code-signing certificate**,
so Windows SmartScreen shows a blue *"Windows protected your PC"* dialog
on first launch. Expected. To proceed:

1. Double-click the `.exe`.
2. SmartScreen dialog appears. Click **More info**.
3. A **Run anyway** button appears below the details. Click it.
4. UAC prompt appears asking to allow the installer to make changes to
   your device. Click **Yes** (the installer needs to write to
   `Program Files\Adobe\...\Plug-ins\Effects\`).
5. Installer runs silently. **Wait ~5 minutes.** The bulk of the time
   is the PyTorch CUDA wheel download (~2 GB over the pip index); the
   installer window doesn't show progress for this, which is normal.
   Task Manager will show a `pip` process doing real work.
6. Installer exits. No further prompts.

Installs land at:
- `C:\Program Files\CorridorKey\` — bundled Python, venv, runtime source, plugin staging copy
- `C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\Effects\CorridorKey.aex` — the effect
- Add/Remove Programs: **CorridorKey** — use this for a clean uninstall

### First run

1. Launch After Effects.
2. Create a comp with a green-screen layer.
3. With the layer selected, **Effect → Keying → CorridorKey**.
4. The effect panel shows a status line at the top. On first frame
   it'll flip from **Starting up** → **Loading engine** → **Loading
   model (...)** while the ~398 MB model weights download in the
   background. First frame takes ~15–30 seconds over a fast connection.
5. Once the status reads **Ready | Xms | WxH**, keying is live. All
   subsequent frames are fast (~165 ms at Fastest, ~558 ms at Full Res
   on an RTX 4090).

Model weights cache at:
- macOS: `~/Library/Application Support/CorridorKey/models/`
- Windows: `%LOCALAPPDATA%\CorridorKey\models\`

So the slow first-run download happens exactly once per user account.

### Uninstalling and starting fresh

Both platforms have a clean-slate script that wipes every artifact the
installer or a dev session could have left behind — install tree,
plugin copies from every AE install, registry receipts, stray runtime
processes, temp files. Useful when you want to re-test an installer
from a known-empty state, or when you just want CorridorKey completely
gone.

**macOS:**
```bash
./scripts/installer/clean_macos.sh
```

**Windows (from an _elevated_ PowerShell — right-click → Run as
administrator):**
```powershell
.\scripts\installer\clean_windows.ps1
```

Neither script installs anything — they just clean. Pair them with a
fresh installer download to test end-to-end. By default the Windows
script preserves `%LOCALAPPDATA%\CorridorKey\models\` (the ~400 MB
cached weights) so you don't have to re-download on the next install;
pass `-KeepModelCache:$false` to wipe those too.

Windows also has a normal **Add/Remove Programs** entry if you just
want to uninstall through Settings. macOS has no equivalent — Apple's
Installer.app doesn't track pkgbuild payloads for removal — so the
clean script is the intended uninstall path there too.

## Remaining Work

See [open issues](https://github.com/iamjoshuadavies/corridorkey-ae/issues) for the full backlog. Key items still open:

- [ ] **Signed + notarized installers** — drops the Gatekeeper
      workaround on macOS and the SmartScreen warning on Windows.
      Needs an Apple Developer ID ($99/yr) and a Windows EV code
      signing cert ($100–300/yr).
- [ ] **Proper GitHub Releases** with checksums and a versioned
      changelog, so users don't have to dig through CI artifacts.
- [ ] **Float32 pipeline** (#10) — skip uint8 quantization for 32bpc projects.

## Credits

Based on the green-screen keying technique created by my friend
**Niko Pueringer** of [Corridor Digital](https://youtube.com/CorridorCrew).
Niko did all the hard research work — the physical unmixing model, the
training, the whole insight that you can actually separate foreground
and background with this kind of fidelity. Model inference runs through
his [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx)
package on macOS and a vendored PyTorch port on Windows.

This After Effects port was started by **Josh Davies**
([@iamjoshuadavies](https://github.com/iamjoshuadavies)) — wrapping
Niko's model in a real AE plugin so compositors can use it the way
they use Keylight. Although Claude did a lot of the work here, let's
be honest. Hopefully more amazing and worthy developers (human or
otherwise) will carry it on from here. PRs welcome.

This project builds against the **Adobe After Effects SDK**, which is
© Adobe Inc. The SDK is not included in this source repository — see
[NOTICE](NOTICE) for third-party attribution details.

## License

[PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) — see [LICENSE](LICENSE).

- ✅ **Use commercially** — use CorridorKey AE on paid VFX/film/video projects
- ✅ **View & modify source** — fork it, improve it, contribute back
- ✅ **Share freely** — distribute to other creatives
- ❌ **Don't sell the tool** — can't resell or build a competing commercial product from it

The upstream [CorridorKey](https://github.com/nikopueringer/CorridorKey) is licensed under CC BY-NC-SA 4.0.
