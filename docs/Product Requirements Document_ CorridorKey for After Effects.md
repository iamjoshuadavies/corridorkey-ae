# Product Requirements Document: CorridorKey for After Effects

**Author:** Manus AI  
**Date:** 2026-04-11  
**Project Working Name:** **CorridorKey AE**

## Executive Summary

This document defines a production-ready foundation for building a **real native Adobe After Effects plugin** inspired by Niko Pueringer’s **CorridorKey**, while incorporating the usability strengths of **EZ-CorridorKey** and the host-integration lessons of **corridorkey_ofx**. The core recommendation is to build the product as a **native After Effects effect plug-in in C++**, backed by a **separate local inference runtime** responsible for model execution, model downloads, hardware detection, caching, and diagnostics. That architecture is the most credible path to shipping a plugin that feels native inside After Effects while remaining maintainable across **macOS and Windows**. It also aligns with the pattern already used successfully by the Resolve OFX wrapper, where a thin host plugin delegates AI inference to a backend process.[1] [2] [4]

A critical finding from the research is that the project should **not** be casually described as “open source” unless licensing is clarified. The upstream CorridorKey repository currently presents a custom **CC BY-NC-SA 4.0-based** license with additional terms, and the existing OFX wrapper states that it matches the upstream license.[4] [8] Because **non-commercial restrictions are not OSI-open-source compliant**, the safest current framing is **public source / source-available / non-commercial**, unless explicit relicensing permission is obtained or the implementation becomes a clean-room rewrite that avoids derivative use of restricted code and assets.[8]

Another important conclusion is that the “6 GB VRAM version” is best understood not as a single canonical hidden fork, but as a **community optimization trajectory** reflected in the current upstream repository and in EZ-CorridorKey’s productization work. The upstream README now states that the most recent build should run on **6–8 GB of VRAM** and on **most M1+ Mac systems with unified memory**, while EZ-CorridorKey documents low-VRAM operation modes, Apple Silicon packaging, memory-aware UI, and installer patterns that are highly relevant to the proposed plugin.[1] [2] [3]

## Product Vision

**CorridorKey AE** should deliver the best parts of CorridorKey directly inside After Effects: physically accurate green-screen separation, host-native effect behavior, clean artist-facing controls, and a low-friction install/update experience. The product should feel like a serious professional plugin rather than a script launcher or ad hoc wrapper. The user should be able to apply the effect to footage, choose an alpha-hint mode, preview outputs, and render with predictable behavior, while the heavier model management and hardware-specific logic are handled by a companion runtime behind the scenes.[1] [2] [4]

The plugin must be designed as a **great foundation for a public collaborative project**. That means deterministic builds, clear repository structure, CI-friendly build tooling, documented platform boundaries, model download strategy, issue templates, release packaging, and a reproducible handoff path for another AI coding agent or human maintainer.[5] [6] [7]

## Problem Statement

Today, CorridorKey exists primarily as a standalone or scriptable inference pipeline rather than a polished native After Effects effect. Community projects have demonstrated stronger UI ideas and host wrapping, but there is not yet a widely adopted, well-documented, modern, cross-platform After Effects implementation that a user can install with confidence and that contributors can build and extend with confidence.[1] [2] [4]

The project therefore needs a disciplined product and engineering specification that transforms three separate inspirations into one coherent plan:

| Source | What it contributes | What it does not solve by itself |
| --- | --- | --- |
| **CorridorKey** | Core model behavior, green-screen separation objective, low-VRAM direction, Apple Silicon feasibility | Native AE integration, productized UX, installer/release discipline inside AE |
| **EZ-CorridorKey** | Strong workflow design, diagnostics, low-VRAM UX, Apple Silicon packaging patterns, release process | Native AE plugin architecture |
| **corridorkey_ofx** | Practical host-wrapper architecture with thin native frontend + backend inference runtime | AE-specific SDK integration and cross-platform implementation |

## Product Goals

The primary product goal is to create a **native After Effects effect plugin** that can be applied like any other effect and that produces CorridorKey-class outputs inside the AE workflow. The product must support **macOS and Windows**, target current After Effects releases, and have a repository structure that enables reliable contributor onboarding and repeatable release builds.[5] [6] [7]

A second goal is to make the plugin operational on realistic creator hardware. The baseline target should be systems with **6–8 GB VRAM** on supported discrete GPUs and **Apple Silicon unified-memory** Macs, while clearly documenting that certain optional models or advanced modes may require more memory.[1] [2] [3]

A third goal is to make distribution straightforward. End users should be able to download **compiled releases** from a public repository, install them without manual SDK steps, and understand whether the plugin itself, the backend runtime, and the model weights are installed locally or downloaded on first use.[3] [6]

## Non-Goals

The first non-goal is embedding the entire AI stack monolithically inside the AE binary if doing so materially increases fragility, signing complexity, or cross-platform risk. The evidence from the OFX implementation strongly favors a native frontend plus managed backend design instead.[4]

The second non-goal is shipping every optional CorridorKey-adjacent model on day one. The first public milestone should prioritize a **practical baseline workflow** rather than attempting to support every heavy experimental module, especially where upstream documentation still notes much higher memory demands for optional paths such as GVM and VideoMaMa.[1]

The third non-goal is promising OSI-compliant open-source licensing before the upstream license position is resolved. The project can still be public and collaborative, but the legal description must be accurate.[8]

## Target Users

The primary users are motion designers, compositors, YouTubers, VFX artists, and post-production generalists who already work inside After Effects and want CorridorKey-class results without switching to a standalone application. A secondary audience is technical artists and pipeline engineers who want a scriptable and debuggable local installation. A third audience is contributors who need a clean, well-scoped codebase to extend, port, or optimize.

## Core User Stories

| User | Story | Success Condition |
| --- | --- | --- |
| AE artist | I apply CorridorKey AE to a green-screen layer and immediately get a meaningful preview | The effect initializes, shows status, and renders a first result without external manual wiring |
| AE artist | I can switch between matte, foreground, processed, and composite outputs | Output changes are fast and do not force unnecessary full re-inference when avoidable |
| AE artist | I can use an external alpha hint from AE when my footage needs more control | The plugin accepts an external hint path or effect-side input workflow and documents it clearly |
| Laptop or mid-range GPU user | I can use a low-VRAM mode instead of simply failing | The plugin detects memory constraints and offers lower-memory execution or fallback behavior |
| Apple Silicon user | I can install and run the plugin on modern Macs with unified memory | The plugin ships as a universal binary and presents memory-aware status information |
| Contributor | I can clone the repository and build the host plugin on macOS or Windows without reverse engineering the project layout | The repo contains a documented, repeatable build system and CI-relevant scripts |
| Maintainer | I can publish compiled releases that users can download directly | The repository produces packaged release artifacts and versioned release notes |

## Product Scope

The recommended v1 scope is intentionally pragmatic. The plugin should expose the keying workflow as an **Effect** plug-in inside After Effects, because Adobe’s SDK documentation still treats the **Skeleton** effect sample as the starting point for effect development.[5] The effect should surface a clear set of controls grouped around alpha generation, inference, quality/performance, and output selection. If the team later needs queue management, model installation dashboards, or richer diagnostics, a separate panel or helper UI can be added, but the first milestone should prove the core native effect experience.

The plugin should not require users to manually run a Python server or copy weights into hidden locations. Instead, the effect should communicate with a local managed runtime that can install or update itself, download required weights with verification, and expose user-readable status codes. EZ-CorridorKey’s release notes offer an excellent pattern here: first-launch setup flows, MLX-aware packaging on macOS, memory-aware UI language, and automatic checkpoint download with validation.[3]

## Recommended Product Architecture

The recommended architecture is a **three-layer system**.

| Layer | Recommendation | Why this is the best foundation |
| --- | --- | --- |
| **Host integration layer** | Native AE **Effect** plug-in in C++ using the official After Effects SDK | This is the correct way to appear as a real effect inside AE and aligns with Adobe’s official samples.[5] |
| **Bridge layer** | Stable IPC between the plugin and a local runtime | This isolates heavy ML dependencies from the plugin binary and matches the proven OFX pattern.[4] |
| **Inference/runtime layer** | Managed local service responsible for model inference, hardware detection, downloads, caches, and logs | This is the safest place to handle PyTorch/MLX/CUDA/MPS/Metal variation without destabilizing AE |

The plugin itself should remain thin. It should own the AE-facing responsibilities: parameter definitions, frame extraction, frame submission to the runtime, receiving results, cache invalidation logic, error surfaces, and final output marshaling. The runtime should own model execution, device selection, memory mode selection, background warmup, compiled kernel caches where applicable, and diagnostics. This separation minimizes the risk that a Python or ML dependency issue will directly corrupt the AE host process.[4]

The bridge may begin with a straightforward local IPC mechanism and later be optimized. The OFX wrapper uses Windows named pipes and shared memory, which is a strong reference point, but the AE project should define a cross-platform abstraction from the beginning rather than adopting a Windows-only transport.[4]

## UX and Interaction Model

The strongest UX references come from EZ-CorridorKey, but they must be translated into After Effects-native interaction patterns. Rather than recreating a full standalone dashboard inside the effect, the plugin should provide **logical control groups**, concise status messaging, and fast output toggles. The experience should feel like a polished AE effect with sensible defaults, while optional advanced diagnostics can appear in a separate companion window or local log viewer.[2] [3]

The effect controls should be organized approximately as follows.

| Group | Parameters |
| --- | --- |
| **Input** | Input colorspace, external alpha hint enable/selection, frame range mode if supported |
| **Alpha Generation** | Auto / external mode, alpha model choice, alpha preprocessing options |
| **Inference** | Quality mode, low-VRAM mode, tile size or memory strategy, device selection or auto-detect |
| **Cleanup** | Despill, despeckle, refiner strength, matte cleanup options |
| **Output** | Processed, Matte, Foreground, Composite, diagnostic preview |
| **System Status** | Device name, VRAM or unified-memory indicator, model status, warmup status, cache state |

A key product lesson from EZ-CorridorKey is that **memory diagnostics must be user-facing**. On Apple Silicon, the UI should say **Memory** rather than **VRAM** when appropriate, because the system uses unified memory.[3] The effect should therefore display hardware status in language that matches the user’s platform.

## Functional Requirements

### Effect Runtime Behavior

The plugin must register as a native After Effects effect and load correctly on supported current AE versions. It must accept an input layer, evaluate frames as needed, and return an output frame to the host. It should provide deterministic behavior in preview and final render modes, with clear status handling for cold-start, warmup, missing weights, unsupported hardware, and runtime failure.

### Output Modes

The plugin must provide at least four user-visible output modes: **Processed**, **Matte**, **Foreground**, and **Composite**. This output taxonomy is consistent with both CorridorKey-style workflows and the OFX wrapper’s published parameter model.[1] [4]

### Alpha Hint Strategy

The plugin must support at least two alpha-hint paths in the first complete version: an **automatic mode** and an **external-hint mode**. External hints are especially important because they provide a pragmatic escape hatch on weaker hardware or for shots where artists want more deterministic input from AE-driven masks.[2] [3] [4]

### Low-VRAM Operation

The plugin must expose a low-VRAM execution path rather than treating memory pressure as an opaque failure. That can include tiling, reduced refiner resolution, more conservative model selection, staged execution, or user-selectable “balanced” versus “low-memory” modes. This requirement is justified by both the upstream project’s 6–8 GB target language and EZ-CorridorKey’s explicit low-VRAM mode framing.[1] [2]

### Install and First-Run Experience

The project must define a first-run workflow in which the plugin can detect whether the local runtime and required weights are present. If not, it should trigger a guided setup path instead of failing silently. The release and packaging behavior of EZ-CorridorKey strongly suggests that automatic download and verification of required model assets should be a first-class requirement.[3]

## Non-Functional Requirements

| Area | Requirement |
| --- | --- |
| **Performance** | Parameter changes that only affect post-processing should avoid full model re-inference whenever possible, following the caching philosophy documented by the OFX wrapper.[4] |
| **Reliability** | Plugin failures must degrade gracefully with explicit status messaging rather than taking down the host. |
| **Cross-platform support** | Windows and macOS must be first-class targets from the beginning. |
| **Mac architecture support** | The macOS build must be a **Universal binary** so it runs on Intel and Apple Silicon hosts.[7] |
| **Installability** | Users must be able to install from downloadable release artifacts without building from source. |
| **Observability** | Runtime logs, version info, and hardware status must be available to users and maintainers. |
| **Maintainability** | The build and release system must be automatable in CI and documented for contributors. |

## Platform and Build Requirements

Adobe’s documentation and community CMake templates support the following implementation direction.

| Topic | Requirement / Recommendation | Source basis |
| --- | --- | --- |
| **Primary SDK base** | Use the official After Effects SDK **Skeleton** effect sample as the semantic starting point | Adobe sample-project guidance.[5] |
| **Build system** | Use **CMake** as the repository-standard build frontend, generating Xcode projects on macOS and Visual Studio solutions on Windows | Community templates show this is practical and CI-friendly.[5] [9] |
| **macOS binary format** | Ship a `.plugin` bundle | Demonstrated by community CMake example and Adobe conventions.[7] [9] |
| **Windows binary format** | Ship a `.aex` | Adobe/community AE build conventions.[9] |
| **Apple Silicon support** | Produce a **universal** macOS build and add `CodeMacARM64 {"EffectMain"}` in PiPL resources | Adobe Apple Silicon guidance.[7] |
| **Install path** | Prefer the common Adobe MediaCore folder when compatible, with AE-only install fallback if the plugin depends on AE-specific APIs | Adobe installer guidance.[6] |

A practical conclusion from the research is that the project should use **official SDK semantics plus community CMake modernization**. In other words, the repository should not invent its own plugin model. It should inherit AE plugin structure from Adobe’s effect sample, inherit build convenience from an existing CMake adaptation, and add project-specific backend/runtime code on top.[5] [9]

## Proposed Repository Structure

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
  LICENSE
  README.md
```

This structure separates **host plugin concerns** from **runtime concerns** and makes it easier to support multiple backend strategies over time. It also creates clean ownership boundaries for contributors and for future AI coding agents.

## Packaging and Release Strategy

The public repository should publish **compiled release artifacts** through GitHub Releases. Each release should include platform-specific archives and explicit release notes. The plugin should not require end users to compile anything locally.

| Release asset | Contents | Notes |
| --- | --- | --- |
| `CorridorKeyAE-macOS-universal.zip` | `.plugin` bundle, installer script or app, README, changelog | Prefer signed and notarized release builds when feasible |
| `CorridorKeyAE-windows-x64.zip` | `.aex`, installer or setup script, runtime bootstrapper, README | Windows SmartScreen friction should be anticipated |
| `checksums.txt` | SHA256 hashes for all release assets | Improves trust and reproducibility |
| `source-code.zip` | Versioned source snapshot | Standard public release practice |

On macOS, signed and notarized builds should be the target for mainstream releases, especially because Adobe’s docs note extra signing considerations for debugging and modern macOS behavior.[7] On both platforms, the release pipeline should clearly separate **plugin binaries**, **runtime dependencies**, and **model weights**. For large model assets, the preferred product pattern is to download them on first run with checksum verification, following the example set by EZ-CorridorKey’s model setup flow.[3]

## Licensing and Governance Requirements

The licensing question is not a footnote; it is a gating requirement. The upstream CorridorKey license page states that the work is licensed under **CC BY-NC-SA 4.0** with additional terms.[8] The Resolve OFX wrapper explicitly states that its own license matches the upstream CorridorKey license.[4] That means the planned AE project should make a conscious decision among three paths:

| Path | Description | Recommendation |
| --- | --- | --- |
| **Derivative public project under compatible non-commercial terms** | Use upstream derivative code/assets and keep distribution consistent with upstream restrictions | Short-term realistic if the goal is a community tool, but do not market it as OSI open source |
| **Permission-based relicensing** | Obtain explicit permission from Niko/Corridor to release the AE implementation under a more permissive license | Best outcome if available |
| **Clean-room reimplementation** | Build an independent implementation around allowed interfaces and models without copying restricted code | Highest legal complexity, but only path to a truly permissive open-source claim without permission |

The PRD recommendation is to **block branding and repository setup decisions on an explicit licensing decision** before implementation begins. This should be Milestone Zero.

> “This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International Public License (CC BY-NC-SA 4.0)” from the upstream license page, with additional terms also shown in the same file.[8]

## Technical Risks

The largest technical risk is host stability. Running modern ML inference inside a DCC host is inherently risky, especially when the dependency stack spans CUDA, Metal, MLX, MPS, PyTorch, and model downloads. A thin native plugin plus managed runtime reduces this risk, but does not eliminate it.[3] [4]

The second major risk is cross-platform IPC and packaging complexity. The OFX wrapper proves the architecture on Windows, but not yet as a cross-platform After Effects implementation. The AE project must therefore define the protocol and lifecycle model with macOS parity from the outset.[4]

The third major risk is legal and governance ambiguity. If contributors believe they are joining a conventional permissive open-source project but the actual dependency chain is non-commercial or derivative-restricted, the project will create confusion and friction. The repository must be honest and explicit from the first commit.

## Milestones

| Milestone | Outcome |
| --- | --- |
| **M0: License and architecture decision** | Confirm legal path, choose derivative vs permission-based route, freeze architecture |
| **M1: Native AE effect shell** | Build and load a minimal effect based on the official Skeleton structure and modernized CMake |
| **M2: Backend bridge** | Establish local runtime process, protocol, and mock inference round-trip |
| **M3: First real inference** | Run actual CorridorKey-compatible processing from AE on one platform |
| **M4: Cross-platform parity** | Bring the same workflow to macOS universal and Windows x64 |
| **M5: Productization** | Add setup flow, model download, diagnostics, status surfaces, and logs |
| **M6: Public releases** | Publish reproducible compiled downloads and contributor documentation |

## Definition of Done for v1

Version 1 is done when a user can download a release artifact, install the plugin on macOS or Windows, apply the effect in current After Effects, complete a documented first-run setup, process footage with at least one automatic and one external-hint workflow, switch among the key output modes, and recover from common failures with understandable messages rather than manual debugging. Contributors must also be able to build the plugin from source using the documented toolchain and obtain matching artifacts through the project’s release scripts.

## Recommended AI-Agent Build Brief

The next AI coding agent should be instructed to treat this project as a **native AE effect plugin with companion runtime**, not as a CEP/UXP panel project and not as a monolithic Python script launcher. The agent should begin by creating a repository that modernizes Adobe’s **Skeleton** effect sample with **CMake**, then implement a thin bridge to a local runtime process, then layer in model installation, diagnostics, and low-VRAM controls.

The agent should be told to preserve strict boundaries:

| Boundary | Rule |
| --- | --- |
| **Host plugin** | Own AE parameter definitions, frame I/O, status UI, and output marshaling |
| **Runtime** | Own model execution, downloads, device selection, caches, logging, and warmup |
| **Release layer** | Own packaging, installers, checksums, release notes, and artifact naming |

The agent should also be instructed that **Apple Silicon universal build support**, **AE install-path correctness**, and **low-VRAM UX** are not later polish items; they are core product requirements backed by the research.[1] [3] [6] [7]

## Implementation Recommendation

If the project starts this week, the strongest practical foundation is:

| Decision | Recommendation |
| --- | --- |
| **Plugin type** | AE **Effect** plug-in |
| **Language for host plugin** | Modern C++ |
| **SDK basis** | Official AE SDK Skeleton effect sample |
| **Build frontend** | **CMake** |
| **macOS build target** | Universal `.plugin` bundle |
| **Windows build target** | x64 `.aex` |
| **Inference strategy** | Local companion runtime rather than fully embedded AI stack |
| **Distribution** | Public GitHub repository + GitHub Releases with compiled downloads |
| **Model delivery** | First-run download with verification, not giant bundled assets by default |
| **Legal posture** | Resolve licensing before claiming “open source” |

This is the most credible route to a maintainable, contributor-friendly project that can actually ship.

## References

[1]: https://github.com/nikopueringer/CorridorKey "GitHub - nikopueringer/CorridorKey: Perfect Green Screen Keys"
[2]: https://github.com/edenaion/EZ-CorridorKey "GitHub - edenaion/EZ-CorridorKey: Perfect Green Screen Keys made EZ!"
[3]: https://github.com/edenaion/EZ-CorridorKey/releases "Releases · edenaion/EZ-CorridorKey"
[4]: https://github.com/gitcapoom/corridorkey_ofx "GitHub - gitcapoom/corridorkey_ofx: OFX plugin bringing CorridorKey's AI green screen keyer into DaVinci Resolve 20"
[5]: https://ae-plugins.docsforadobe.dev/intro/sample-projects/ "Sample Projects - After Effects C++ SDK Guide"
[6]: https://ae-plugins.docsforadobe.dev/intro/where-installers-should-put-plug-ins/ "Where Installers Should Put Plug-ins - After Effects C++ SDK Guide"
[7]: https://ae-plugins.docsforadobe.dev/intro/apple-silicon-support/ "Apple Silicon Support - After Effects C++ SDK Guide"
[8]: https://github.com/nikopueringer/CorridorKey/blob/main/LICENSE "CorridorKey/LICENSE at main · nikopueringer/CorridorKey · GitHub"
[9]: https://github.com/mobile-bungalow/after_effects_cmake "GitHub - mobile-bungalow/after_effects_cmake: A Cmake file set up to build the Skeleton example project from the After Effects SDK."
