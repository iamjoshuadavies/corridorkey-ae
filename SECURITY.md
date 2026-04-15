# Security policy

## Supported versions

CorridorKey AE is pre-1.0. Only the **latest release** on the
[Releases page](https://github.com/iamjoshuadavies/corridorkey-ae/releases/latest)
is supported with security fixes. Older versions should be upgraded.

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub
issues, discussions, or pull requests.**

Instead, report them privately through GitHub's
[private vulnerability reporting](https://github.com/iamjoshuadavies/corridorkey-ae/security/advisories/new)
feature. This opens a private thread between you and the maintainers
where we can triage, patch, and coordinate disclosure without exposing
other users in the meantime.

When you report, please include:

- A clear description of the issue
- Steps to reproduce (or a proof of concept, if you have one)
- The version of CorridorKey AE you tested (`v0.1.0`, `main@<sha>`, etc.)
- Your platform (macOS version + AE version, or Windows version + AE
  version + GPU)
- Any mitigations you've already tried

We'll acknowledge the report within a few days and work with you on a
fix and coordinated disclosure timeline. We don't currently run a bug
bounty program, but we'll credit you in the release notes for the fix
unless you prefer to remain anonymous.

## Scope

In scope:

- The CorridorKey AE plugin binary (`.plugin` / `.aex`)
- The Python runtime (`runtime/`)
- The IPC protocol between the plugin and the runtime
- The installer scripts and InnoSetup / pkgbuild configurations
- GitHub Actions workflows (CI, release publishing)

Out of scope (report upstream instead):

- The Adobe After Effects SDK or After Effects itself — report to Adobe
- The upstream [`corridorkey-mlx`](https://github.com/nikopueringer/corridorkey-mlx)
  package — report on that repo
- PyTorch, MLX, or other third-party dependencies — report upstream

## Threat model

CorridorKey AE runs entirely locally. The runtime binds to `127.0.0.1`
on a loopback-only socket with an ephemeral port, never accepts
connections from non-local clients, and has no network I/O at runtime
other than the one-time model weights download from the upstream
GitHub release on first frame.

Things we care about:

- Untrusted input via the IPC protocol from the plugin (bounds checks,
  length-prefix validation, memory safety)
- Model weights download integrity (we pin the upstream release tag and
  verify the file exists in the expected cache location)
- Installer privilege handling (installers need admin / root to write
  into AE's Plug-Ins folder — any escalation path beyond that is a bug)
- Process-tree cleanup (runtime subprocesses should not outlive the AE
  host on either platform)

Things that are explicit non-goals:

- Sandboxing the Python runtime from the rest of the machine — it runs
  with the AE host's privileges, which is standard for AE plugins
- Protecting the model weights as a trade secret — they're published
  under a permissive license upstream
