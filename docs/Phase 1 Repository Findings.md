# Phase 1 Repository Findings

## Original CorridorKey (`nikopueringer/CorridorKey`)

The current upstream repository is substantially more modern than an early proof of concept and already states that **the most recent build should work on computers with 6–8 GB of VRAM** and on **most M1+ Mac systems with unified memory**. This suggests that the desired low-VRAM variant may not be a separate fork anymore, but rather upstreamed optimizations or a community-maintained branch that later informed the main repository.

Key observations from the repository landing page:

- The project still emphasizes very high-end requirements for optional modules such as **GVM** and **VideoMaMa**, while indicating that community optimization has reduced practical requirements for the main workflow.
- The commit history references **GPU enumeration with VRAM reporting**, implying active work on hardware-awareness and memory-sensitive execution.
- The repository contains distinct modules such as `CorridorKeyModule`, `BiRefNetModule`, `VideoMaMaInferenceModule`, `backend`, and `gvm_core`, which indicates a modular Python-based application rather than a native host-plugin architecture.
- The repo currently appears aimed at a standalone or scriptable pipeline, not an Adobe After Effects native effect/plugin.

Implication for the AE plugin PRD: the plugin should probably treat the existing CorridorKey codebase as a **backend engine reference** rather than as drop-in plugin code.

## EZ-CorridorKey (`edenaion/EZ-CorridorKey`)

This fork appears highly relevant for **product and UX inspiration**.

Key observations from the repository landing page:

- It is significantly ahead of upstream in commit count, suggesting active independent productization and UX work.
- The README prominently defines a full application layout with the following concepts:
  - **Brand bar + menu**
  - **GPU name + VRAM meter**
  - **Queue panel**
  - **Dual viewer** with input and output views
  - **Parameter panel** grouped by Alpha Generation, Inference, and Output
  - **Thumbnail I/O tray**
  - **Status bar** and explicit **Run Inference** action
- The repo references **MPS support**, **MLX macOS packaging**, **GPU guards**, **diagnostic systems**, **SAM2 annotation tracking**, and installer hardening, which may be especially useful when defining a robust cross-platform open-source project foundation.

Implication for the AE plugin PRD: the plugin should borrow the **hardware diagnostics**, **VRAM awareness**, **guided workflow**, and **clear grouped controls** from EZ-CorridorKey, but adapt them to After Effects paradigms such as effect controls, panels, background tasks, and render-state feedback.

## Early conclusion on the user's “6 GB VRAM version” request

Based on the upstream README, the low-VRAM capability may already be reflected in the modern upstream repository rather than existing only as a distinct separate fork. However, this still needs validation through broader research across forks, issues, PRs, releases, and discussions.

## EZ-CorridorKey release and packaging findings

The releases page adds concrete evidence that EZ-CorridorKey has become a serious distribution reference, not just a UI mockup. Its release notes describe a **macOS app bundle**, **Windows installer**, **first-launch setup wizard**, **standardized asset naming**, and **Apple Silicon-specific memory reporting**. The project explicitly distinguishes Apple Silicon **unified memory** from discrete GPU VRAM and bundles MLX-related runtime pieces directly into the application package. These patterns are directly useful for an open-source AE companion architecture, especially if the final system uses a native plugin shell plus a separately managed inference runtime.

Most importantly, the release notes confirm practical product decisions that should influence the PRD: automatic model downloads with verification, graceful fallback for older MLX runtime capabilities, explicit chip detection, and user-facing memory diagnostics. These are precisely the sorts of “boring but critical” features that make an open-source creative tool buildable and supportable.

## `corridorkey_ofx` architecture findings

The OFX project is highly relevant as an **implementation pattern**, even if it is not directly reusable for After Effects. Its README describes a **thin C++ host plugin** responsible for UI and frame I/O, paired with a **background Python process** that runs PyTorch inference. Communication is handled through **inter-process communication** rather than embedding the entire ML stack into the plugin binary. The repository also documents installation, file locations, warmup behavior, caching strategy, and backend auto-launch behavior.

This strongly suggests that the safest architecture for an After Effects project is not a monolithic plugin that statically embeds all AI dependencies. Instead, a more robust design would likely use a **native AE plugin for host integration** and a **managed backend service or companion runtime** for model execution, downloads, updates, and hardware-specific logic. The OFX project also shows that installers, backend lifecycle management, and cache-aware parameter changes are first-class product requirements, not optional implementation details.

## Refined phase conclusion

The current evidence indicates that the desired low-VRAM direction is not a single hidden fork so much as a **cluster of community optimizations** now reflected across the upstream project and especially in EZ-CorridorKey. For the PRD, the most credible position is that the plugin should target a baseline workflow that is realistic on **6–8 GB VRAM systems and Apple Silicon unified memory**, while clearly documenting that certain optional models or modes remain substantially heavier.
