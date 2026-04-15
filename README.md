<p align="center">
  <img src="plugin/resources/logo_256.png" alt="CorridorKey" width="128" height="128">
</p>

<h1 align="center">CorridorKey AE</h1>

<p align="center">
  Native Adobe After Effects plugin for advanced green-screen keying.<br>
  Based on the technique created by my friend <strong>Niko Pueringer</strong> of <a href="https://youtube.com/CorridorCrew">Corridor Digital</a>.
</p>

<p align="center">
  <a href="https://github.com/iamjoshuadavies/corridorkey-ae/releases/latest">
    <img src="https://img.shields.io/github/v/release/iamjoshuadavies/corridorkey-ae?include_prereleases&sort=semver&cacheSeconds=60&_=1" alt="Latest release">
  </a>
  <a href="https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml">
    <img src="https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml/badge.svg" alt="CI">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-PolyForm_NC_1.0.0-blue.svg" alt="License: PolyForm Noncommercial 1.0.0">
  </a>
</p>

<p align="center">
  <a href="https://github.com/iamjoshuadavies/corridorkey-ae/releases/latest"><strong>⬇ Download the latest release</strong></a>
  &nbsp;·&nbsp;
  <a href="#install">Install</a>
  &nbsp;·&nbsp;
  <a href="#first-run">First run</a>
  &nbsp;·&nbsp;
  <a href="#troubleshooting">Troubleshooting</a>
  &nbsp;·&nbsp;
  <a href="CONTRIBUTING.md">Build from source</a>
</p>

---

CorridorKey AE brings physically accurate green-screen separation directly
into After Effects. Apply the effect, optionally point it at an alpha hint
layer, and get production-quality keying with foreground extraction,
despill, and matte cleanup — all powered by ML inference running locally
on your machine. No cloud, no account, no subscription.

> **Status:** v0.1.0 shipped. Keying pipeline working end-to-end on both
> platforms. Installers ship unsigned — expect one Gatekeeper /
> SmartScreen prompt on first launch (see [Install](#install)).

## Download

Get the latest installer from the
[**Releases page**](https://github.com/iamjoshuadavies/corridorkey-ae/releases/latest).
Every release bundles both platform installers plus SHA-256 checksums.

| Platform | File | Size |
|---|---|---|
| macOS (Apple Silicon) | `CorridorKey-<version>-macOS-arm64.pkg` | ~18 MB |
| Windows (x64, NVIDIA) | `CorridorKey-<version>-windows-x64.exe` | ~28 MB |

No Python install needed, no command line, no environment variables —
the installer bundles Python, creates a dedicated virtualenv, pip-installs
all dependencies, and drops the plugin into After Effects automatically.
The only thing not bundled is the model weights (~398 MB), which download
on first frame and cache locally so subsequent launches are instant.

> **Daily builds from `main`** are also available as workflow artifacts
> on the [Actions tab](https://github.com/iamjoshuadavies/corridorkey-ae/actions/workflows/ci.yml)
> if you want the bleeding edge between releases. Open the most recent
> green run and grab `CorridorKey-Installer-macOS` /
> `CorridorKey-Installer-Windows` from the Artifacts section. Requires a
> signed-in GitHub account.

## System requirements

| Platform | Minimum | Verified on |
|---|---|---|
| **macOS** | Apple Silicon (M1/M2/M3/M4/M5), macOS 11 Big Sur or later, After Effects 2024 or later | MacBook Pro M5, AE 2026 |
| **Windows** | x64 CPU, NVIDIA GPU with CUDA 12.1 support, Windows 10 1803+ or Windows 11, After Effects 2024 or later | RTX 4090, Windows 11, AE 2026 |

**Not supported:** Intel Macs, AMD/Intel GPUs on Windows, CPU-only
inference, Linux. See [FAQ](#faq) for why.

## Install

### macOS

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

Install locations:
- `/Library/Application Support/CorridorKey/` — bundled Python, venv, runtime source
- `/Applications/Adobe After Effects <version>/Plug-Ins/Effects/CorridorKey.plugin` — the effect
- `/Library/Logs/CorridorKey-install.log` — installer log (useful if something breaks)

### Windows

The installer is **not yet signed with an EV code-signing certificate**,
so Windows SmartScreen shows a blue *"Windows protected your PC"* dialog
on first launch. Expected. To proceed:

1. Double-click the `.exe`.
2. SmartScreen dialog appears. Click **More info**.
3. A **Run anyway** button appears below the details. Click it.
4. UAC prompt appears asking to allow the installer to make changes to
   your device. Click **Yes** (the installer needs to write to
   `Program Files\Adobe\...\Plug-ins\Effects\`).
5. Installer runs. **Wait ~5 minutes.** The bulk of the time is the
   PyTorch CUDA wheel download (~2 GB over the pip index); the installer
   window doesn't show progress for this, which is normal. Task Manager
   will show a `pip` process doing real work.
6. Installer exits. No further prompts.

Install locations:
- `C:\Program Files\CorridorKey\` — bundled Python, venv, runtime source, plugin staging copy
- `C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\Effects\CorridorKey.aex` — the effect
- Add/Remove Programs: **CorridorKey** — clean uninstall path

## First run

CorridorKey works by pairing your green-screen footage with a rough
*alpha hint* matte. The hint doesn't need to be perfect — it just
tells the model roughly where the foreground is — and Keylight is
the quickest way to make one.

1. **Open After Effects** and create a new composition.
2. **Bring your green-screen clip into the comp.**
3. **Duplicate the layer** so you have two copies stacked on top of
   each other, perfectly aligned.
4. **Rename the top layer to `hint`.** This one becomes the alpha
   hint; the bottom one is what CorridorKey actually processes.
5. **On the `hint` layer, apply Keylight** (*Effect → Keying →
   Keylight (1.2)*) and pull a rough key on the green. Don't worry
   about edges or spill — a fast, loose key is all you need.
6. **Set Keylight's View to *Screen Matte*.** The layer should now
   show a black-and-white silhouette of your subject. That's the
   hint.
7. **Hide the `hint` layer** (click the eyeball) so it doesn't
   contribute to the comp visually — CorridorKey still reads its
   pixels as input.
8. **Select the bottom layer** and apply **Effect → Keying →
   CorridorKey**.
9. **In CorridorKey's Alpha Hint dropdown**, pick the `hint` layer.
   Right next to the dropdown there's a source selector — **set it
   to `Effects & Masks`** so CorridorKey reads the Keylight output,
   not the raw source pixels.
10. That's it. The status line in the effect panel flips through
    **Starting up** → **Loading engine** → **Loading model (...)**
    on first frame while the ~398 MB model weights download from
    the upstream [corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx)
    GitHub release (~15–30 seconds over a fast connection). Once
    the status reads **Ready | Xms | WxH**, keying is live and
    subsequent frames are fast (~165 ms at Fastest, ~600 ms at Full
    Res on an RTX 4090).

> **Tip:** once you've got a result you like, switch **Output Mode**
> through *Matte*, *Foreground*, and *Composite* to inspect each
> stage. *Processed* (the default) is what you'll use in most comps.

Model weights cache here and download exactly once per user account:

- macOS: `~/Library/Application Support/CorridorKey/models/`
- Windows: `%LOCALAPPDATA%\CorridorKey\models\`

## Features

- **Cross-platform** — macOS (Apple Silicon) and Windows (x64/NVIDIA)
- **Local inference** — MLX on Apple Silicon, PyTorch/CUDA on Windows. Nothing leaves your machine.
- **Native AE effect** — Smart Render, Multi-Frame Rendering, 8/16/32bpc float support
- **Tiled full-resolution output** — works at 1080p, 4K, and beyond
- **Quality presets** — Fastest (256) → Fast (512) → High (1024) → Full Res (Tiled)
- **Alpha hint input** — precomp a rough key with any other tool and feed it in
- **Post-processing** — Despill, Despeckle, Refiner, Matte Cleanup (all re-applied without re-running the model)
- **Two-tier frame caching** — raw model output and post-processed response cached independently
- **Auto-launch runtime** — no manual server start, no console windows
- **Auto-download weights** — first frame pulls the model, then it's offline forever

### Effect controls

| Control | Description |
|---------|-------------|
| **Output Mode** | Processed / Matte / Foreground / Composite |
| **Alpha Hint** | Layer input for an external alpha matte |
| **Quality** | Fastest (256) → Full Res (Tiled) |
| **Despill** | Remove green spill from edges (0–1) |
| **Despeckle** | Remove small matte noise (0–1) |
| **Refiner** | Edge refinement strength (0–1) |
| **Matte Cleanup** | Tighten and smooth matte edges (0–1) |

## Performance

**macOS (MacBook Pro M5 via MLX)** — tiled inference, working resolution per preset:

| Quality Mode | Resolution | Speed (1080p input) | Use case |
|---|---|---|---|
| Fastest (256) | Downscale to 256 | ~460 ms | Scrubbing, quick preview |
| Fast (512) | Downscale to 512 | ~460 ms | Interactive work |
| High (1024) | Downscale to 1024 | ~760 ms | Higher-quality preview |
| Full Res (Tiled) | Native resolution | ~4.8 s | Final render, production |

(Fastest and Fast are effectively the same on M5 — at these sizes the model
runs at the same effective rate, so there's little benefit to 256 over 512.)

**Windows (RTX 4090 via PyTorch CUDA, fp16)** — each preset switches to a
different model size (pos_embed bicubic-interpolated at load time):

| Quality Mode | Model | Speed (1080p input) |
|---|---|---|
| Fastest (256) | 512, no refiner | ~187 ms |
| Fast (512) | 512 + refiner | ~230 ms |
| High (1024) | 1024 + refiner | ~286 ms |
| Full Res | 2048 + refiner | ~612 ms |

All three model sizes stay live on the GPU together (~0.5 GB total VRAM)
so switching Quality doesn't trigger a reload. Cached frames return
instantly. Changing post-processing sliders (despill, despeckle, cleanup)
skips model inference and re-applies only the cheap post-processing
(~10 ms).

## Troubleshooting

**The effect panel shows `Bridge error` and never progresses.**
The Python runtime couldn't start. Check the runtime log — it gets rewritten each launch:

- macOS: `~/Library/Logs/CorridorKey-runtime.log` (and `/Library/Logs/CorridorKey-install.log` for installer issues)
- Windows: `%TEMP%\corridorkey_runtime.log`

Most common cause on Windows is an incomplete PyTorch install — the
installer's pip step timed out during the CUDA wheel download. Uninstall
via Add/Remove Programs and reinstall.

**Status sticks on `Loading model (...)` for more than a minute.**
The model weights are downloading from GitHub. Check your network. If a
corporate proxy is blocking `github.com` or
`objects.githubusercontent.com`, the runtime can't fetch the weights.
Workaround: download the safetensors manually from the
[corridorkey-mlx releases](https://github.com/nikopueringer/corridorkey-mlx/releases)
and drop it in the cache directory (see [First run](#first-run)).

**Windows: the effect applies but the output looks washed out / "milky".**
Old builds skipped ImageNet normalization on the RGB input; this was
fixed pre-v0.1.0. If you're seeing this on v0.1.0 or later, please
[open an issue](https://github.com/iamjoshuadavies/corridorkey-ae/issues/new/choose).

**Windows: `torch.cuda.is_available()` is false.**
Your NVIDIA driver is too old for CUDA 12.1. Update via GeForce
Experience or from
[nvidia.com/drivers](https://www.nvidia.com/drivers).
Verify with `nvidia-smi` — the top-right "CUDA Version" needs to read
12.1 or higher.

**macOS: "After Effects has detected unknown plugin" or CorridorKey doesn't appear in the Effect menu.**
AE cached the plugin as "Ignore" from a previous broken load. Restart AE
while holding **Cmd+Option+Shift** to reset preferences, or reinstall.

**AE hangs for a few seconds when I drop the effect on a layer.**
First-ever apply launches the runtime subprocess and loads the engine in
the background. The status line in the effect panel updates while that
happens. Subsequent applies are instant.

**How do I completely remove CorridorKey and start fresh?**
Run the clean-slate script (see [Uninstall](#uninstall-and-start-fresh))
or use Add/Remove Programs on Windows.

## Uninstall and start fresh

Both platforms have a clean-slate script that wipes every artifact the
installer or a dev session could have left behind — install tree, plugin
copies from every AE install, registry receipts, stray runtime
processes, temp files. Useful when you want to re-test an installer from
a known-empty state, or when you just want CorridorKey completely gone.

**macOS:**
```bash
./scripts/installer/clean_macos.sh
```

**Windows** (from an *elevated* PowerShell — right-click → Run as administrator):
```powershell
.\scripts\installer\clean_windows.ps1
```

Neither script installs anything — they just clean. Pair them with a
fresh installer download to test end-to-end. By default the Windows
script preserves `%LOCALAPPDATA%\CorridorKey\models\` (the ~400 MB
cached weights) so you don't have to re-download on the next install;
pass `-KeepModelCache:$false` to wipe those too.

Windows also has a normal **Add/Remove Programs** entry if you just want
to uninstall through Settings. macOS has no equivalent — Apple's
Installer.app doesn't track `pkgbuild` payloads for removal — so the
clean script is the intended uninstall path there.

## FAQ

**Does it work on Intel Macs?**
No. The macOS build targets Apple Silicon only — MLX is Apple Silicon
exclusive, and the installer ships an arm64 Python. An Intel Mac fallback
would need a Metal/CoreML port of the model and a universal binary build;
no plans currently.

**Does it work on AMD or Intel GPUs on Windows?**
No. The Windows engine is PyTorch + CUDA, so NVIDIA with a driver that
supports CUDA 12.1 is required. A DirectML or ONNX Runtime backend would
unlock AMD/Intel GPUs but isn't currently on the roadmap.

**Is there a CPU fallback?**
No. The model is too heavy for practical CPU inference — you'd be looking
at tens of seconds per frame even at the smallest quality preset. If you
don't have a supported GPU, CorridorKey won't help you.

**Does it work on Linux?**
The runtime is Python and would run fine on Linux, but Adobe doesn't ship
After Effects for Linux, so there's no host to plug into.

**Does data leave my machine?**
No. All inference runs locally. The only network traffic is the one-time
model weights download from the
[corridorkey-mlx](https://github.com/nikopueringer/corridorkey-mlx)
GitHub release on first frame. Nothing phones home, nothing is uploaded,
nothing is tracked.

**Can I use CorridorKey AE on paid work?**
Yes. PolyForm Noncommercial restricts *selling the tool itself*, not
*using the tool on commercial projects*. Use it on as many paid VFX /
film / video jobs as you want. See [License](#license) below.

**How do I report a bug?**
[Open an issue](https://github.com/iamjoshuadavies/corridorkey-ae/issues/new/choose)
— there are templates for bug reports and feature requests. Include your
OS version, AE version, GPU, and the runtime log if possible.

**How do I contribute?**
See [CONTRIBUTING.md](CONTRIBUTING.md) for the build-from-source path,
test suite, code style, and PR workflow.

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

Three layers, cleanly separated:

- **`plugin/`** — C++ After Effects effect. Registers params, handles
  Smart Render + MFR, runs the custom Drawbot UI, manages the bridge
  socket to the runtime.
- **`runtime/`** — Python inference service. MLX engine on macOS
  (`corridorkey_mlx` package), PyTorch engine on Windows (vendored
  GreenFormer + weights loader that converts from the upstream MLX
  safetensors at load time).
- **`shared/`** — IPC protocol definitions, shared by both layers.

Full architecture notes and design decisions live in
[`CONTRIBUTING.md`](CONTRIBUTING.md#architecture) and the per-area
docstrings in the source.

## Build from source

See [**CONTRIBUTING.md**](CONTRIBUTING.md) for the full build, test, and
PR workflow on both platforms. In short:

```bash
# macOS
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DAE_SDK_PATH=/path/to/ae_sdk
cmake --build build

# Windows
cmake -B build_win -S . -G "Visual Studio 16 2019" -A x64 `
      -DAE_SDK_PATH="C:/path/to/AfterEffectsSDK/Examples"
cmake --build build_win --config Release
```

You'll need the Adobe After Effects SDK (free, requires registration at
[Adobe Developer Console](https://developer.adobe.com/console/)). The SDK
is not redistributable and is not included in this repository.

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
Niko's model in a real AE plugin so compositors can use it the way they
use Keylight. Although Claude did a lot of the work here, let's be
honest. Hopefully more amazing and worthy developers (human or
otherwise) will carry it on from here. PRs welcome.

This project builds against the **Adobe After Effects SDK**, which is
© Adobe Inc. The SDK is not included in this source repository — see
[NOTICE](NOTICE) for third-party attribution details.

## Security

See [SECURITY.md](SECURITY.md) for how to report security issues
privately.

## License

[PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) — see [LICENSE](LICENSE).

- ✅ **Use commercially** — use CorridorKey AE on paid VFX/film/video projects
- ✅ **View & modify source** — fork it, improve it, contribute back
- ✅ **Share freely** — distribute to other creatives
- ❌ **Don't sell the tool** — can't resell or build a competing commercial product from it

The upstream [CorridorKey](https://github.com/nikopueringer/CorridorKey)
is licensed under CC BY-NC-SA 4.0.
