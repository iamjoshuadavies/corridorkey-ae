<#
.SYNOPSIS
    Clean-only wrapper around clean_and_test_windows.ps1.

.DESCRIPTION
    Wipes every CorridorKey artifact on the machine (install tree at
    %ProgramFiles%\CorridorKey\, Phase 4 dev tree at
    %LOCALAPPDATA%\CorridorKey\, the .aex from every AE install,
    InnoSetup registry receipts, stray runtime processes, temp files).
    Does NOT install anything afterwards - leaves the machine in a
    verified clean state so you can download a fresh installer and
    test it by hand.

    Equivalent to `clean_and_test_windows.ps1 -CleanOnly`; this wrapper
    exists so the intent is obvious from the filename.

    By default the cached model weights at
    %LOCALAPPDATA%\CorridorKey\models\ are preserved so the first frame
    after reinstall doesn't have to re-download 400 MB. Pass
    -KeepModelCache:$false to also wipe the weights and exercise the
    full first-run download path.

    Requires an elevated PowerShell. Close After Effects first.

.EXAMPLE
    # Standard: clean everything except the weight cache
    .\scripts\installer\clean_windows.ps1

.EXAMPLE
    # Nuke the weight cache too for a true first-run test
    .\scripts\installer\clean_windows.ps1 -KeepModelCache:$false
#>

[CmdletBinding()]
param(
    [switch]$KeepModelCache = $true
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$target = Join-Path $scriptDir 'clean_and_test_windows.ps1'
& $target -CleanOnly -KeepModelCache:$KeepModelCache
