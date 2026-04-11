# AI Agent Handoff: Build CorridorKey AE

You are building **CorridorKey AE**, a **native Adobe After Effects effect plugin** inspired by CorridorKey, informed by EZ-CorridorKey’s UX, and architecturally informed by corridorkey_ofx. Treat this as a real cross-platform plugin project, not as a quick wrapper script.

## Mission

Create a repository that can produce:

| Platform | Deliverable |
| --- | --- |
| macOS | Universal `.plugin` bundle for current After Effects |
| Windows | x64 `.aex` for current After Effects |

The project must be structured so that compiled binaries can be published as downloadable GitHub Release artifacts.

## Hard Requirements

| Area | Requirement |
| --- | --- |
| Plugin type | Native **AE Effect** plug-in in C++ |
| Build system | **CMake** as the repo-standard build entry point |
| SDK basis | Official AE SDK **Skeleton** effect sample semantics |
| Runtime architecture | Thin native plugin + separate local inference runtime |
| Platforms | macOS and Windows from day one |
| macOS support | Universal binary with Intel + Apple Silicon support |
| UX | AE-native controls plus clear setup/status/error messaging |
| Low-memory support | Explicit low-VRAM or low-memory mode |
| Distribution | Public repository with compiled release artifacts |

## Constraints

Do **not** start with CEP or UXP as the primary product. If a panel is added later, it is secondary. The main product is the effect itself.

Do **not** embed the full ML stack directly into the AE plugin binary unless there is a very strong later justification. The preferred architecture is a plugin frontend that talks to a local runtime process.

Do **not** claim the project is permissively open source until the upstream licensing situation is resolved.

## Architecture to Implement

Build a three-layer system.

| Layer | Responsibilities |
| --- | --- |
| Host plugin | AE effect registration, parameters, frame extraction, output rendering, user-visible status |
| Bridge | Local IPC, request/response schema, process lifecycle, health checks |
| Runtime | Model execution, hardware detection, downloads, caches, logs, low-memory strategy |

The plugin should stay thin and stable. The runtime should own most AI-specific complexity.

## Initial Repository Layout

```text
corridorkey-ae/
  plugin/
    src/
    include/
    resources/
    CMakeLists.txt
  runtime/
    server/
    engines/
    models/
    installers/
    tests/
  shared/
    protocol/
    schema/
  scripts/
    bootstrap/
    package/
    release/
  docs/
    architecture/
    contributor-guide/
    release-process/
  .github/
    workflows/
  README.md
```

## v1 Feature Scope

Implement the following effect control groups.

| Group | Required capabilities |
| --- | --- |
| Input | Input handling and external alpha-hint pathway |
| Alpha | Auto mode and external-hint mode |
| Inference | Device auto mode, low-memory mode, quality/performance switch |
| Cleanup | Matte cleanup and basic post-processing knobs |
| Output | Processed, Matte, Foreground, Composite |
| Status | Model state, device state, warmup state, error state |

## Engineering Plan

### Milestone 0

Resolve licensing posture and record it explicitly in the repository.

### Milestone 1

Create a minimal AE effect plugin based on the official Skeleton sample and modernize the build with CMake. Confirm it builds on macOS and Windows.

### Milestone 2

Create a local runtime process with a simple health endpoint or ping mechanism and wire the plugin to it through a stable protocol.

### Milestone 3

Implement a mock frame-processing path first. Verify plugin-to-runtime-to-plugin round-trip behavior before integrating real model inference.

### Milestone 4

Integrate real CorridorKey-compatible inference behavior. Add low-memory mode and user-facing diagnostics.

### Milestone 5

Add first-run setup, model download with verification, release packaging, and contributor documentation.

## Required Build and Release Outputs

| Output | Requirement |
| --- | --- |
| macOS debug build | Local developer build of universal `.plugin` |
| macOS release build | Packaged artifact suitable for GitHub Releases |
| Windows debug build | Local developer build of `.aex` |
| Windows release build | Packaged artifact suitable for GitHub Releases |
| Checksums | SHA256 for all release artifacts |
| Documentation | Clear install and build instructions |

## Definition of Done

The project is ready for first public release when a user can download a compiled artifact, install it on macOS or Windows, apply the effect in current After Effects, complete any required first-run setup, run at least one automatic and one external-hint workflow, and get meaningful output without touching source code.

## Quality Bar

Prioritize host stability, clarity, and maintainability over feature sprawl. The plugin should feel professional, deterministic, and debuggable. If a feature threatens stability, move it behind an advanced or experimental path rather than contaminating the core effect workflow.

## Source Material to Consult

| Source | URL |
| --- | --- |
| CorridorKey | https://github.com/nikopueringer/CorridorKey |
| EZ-CorridorKey | https://github.com/edenaion/EZ-CorridorKey |
| corridorkey_ofx | https://github.com/gitcapoom/corridorkey_ofx |
| Adobe sample projects | https://ae-plugins.docsforadobe.dev/intro/sample-projects/ |
| Adobe install guidance | https://ae-plugins.docsforadobe.dev/intro/where-installers-should-put-plug-ins/ |
| Adobe Apple Silicon guidance | https://ae-plugins.docsforadobe.dev/intro/apple-silicon-support/ |
| Community CMake reference | https://github.com/mobile-bungalow/after_effects_cmake |

## Final Instruction

Start by scaffolding the repository, documenting the architecture, and building the minimal native effect shell with CMake. Do not skip the build foundation.
