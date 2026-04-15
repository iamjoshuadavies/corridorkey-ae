<#
.SYNOPSIS
    Clean-slate + install-test harness for the Windows .exe installer.

.DESCRIPTION
    Mirror of scripts/installer/clean_and_test_macos.sh. Wipes every
    CorridorKey artifact the installer or dev workflow could have left
    on the machine, then runs a .exe installer and verifies the result.
    Lets you go from "worked on my machine" to "actually fresh install"
    in one command.

    Requires elevation (admin PowerShell). Close After Effects first.

.PARAMETER ExePath
    Path to a specific .exe to install. Skips auto-detection.

.PARAMETER FromCi
    Download the latest CorridorKey-Installer-Windows artifact from a
    successful CI run and install it. Requires `gh` CLI in PATH.

.PARAMETER RunId
    Pair with -FromCi to pull from a specific CI run instead of the
    latest successful one.

.PARAMETER CleanOnly
    Just clean, skip the install step.

.PARAMETER KeepModelCache
    Preserve %LOCALAPPDATA%\CorridorKey\models\ (the ~400 MB weights
    download) across the clean. Default is on — you only want this off
    when you're also testing the first-run download path.

.EXAMPLE
    # Clean only
    .\scripts\installer\clean_and_test_windows.ps1 -CleanOnly

.EXAMPLE
    # Clean + install the most recent local build
    .\scripts\installer\clean_and_test_windows.ps1

.EXAMPLE
    # Clean + install a specific .exe
    .\scripts\installer\clean_and_test_windows.ps1 -ExePath C:\path\to\CorridorKey-0.1.0-windows-x64.exe

.EXAMPLE
    # Clean + install the latest CI-built .exe
    .\scripts\installer\clean_and_test_windows.ps1 -FromCi

.EXAMPLE
    # Force a full first-run experience (nukes the cached model weights)
    .\scripts\installer\clean_and_test_windows.ps1 -KeepModelCache:$false
#>

[CmdletBinding()]
param(
    [string]$ExePath = "",
    [switch]$FromCi,
    [string]$RunId = "",
    [switch]$CleanOnly,
    [switch]$KeepModelCache = $true
)

$ErrorActionPreference = "Stop"
$Repo = "iamjoshuadavies/corridorkey-ae"

function Say   ([string]$Msg) { Write-Host "==> $Msg" -ForegroundColor Cyan }
function Ok    ([string]$Msg) { Write-Host " OK $Msg"  -ForegroundColor Green }
function Warn  ([string]$Msg) { Write-Host " !! $Msg"  -ForegroundColor Yellow }
function Fail  ([string]$Msg) { Write-Host " XX $Msg"  -ForegroundColor Red; exit 1 }

# --- Preflight --------------------------------------------------------------

# Admin?
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
if (-not $isAdmin) {
    Fail "This script must run from an elevated PowerShell (Run as administrator). The installer writes to Program Files."
}

# AE running?
$ae = Get-Process -Name "AfterFX" -ErrorAction SilentlyContinue
if ($ae) {
    Fail "After Effects is running (AfterFX.exe, PID $($ae.Id)). Quit it and re-run this script."
}

# --- 1. Kill any stray runtime processes ------------------------------------
Say "Killing any running CorridorKey runtime processes"
$killed = 0
Get-CimInstance Win32_Process -Filter "name='python.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.CommandLine -and $_.CommandLine -match "server\.main") {
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        $killed++
    }
}
if ($killed -gt 0) { Ok "Killed $killed runtime process(es)" }
else { Ok "No runtime processes running" }

# --- 2. Remove the plugin from every AE install ----------------------------
Say "Removing CorridorKey.aex from all AE Plug-ins\Effects folders"
$adobe = Join-Path ${env:ProgramFiles} 'Adobe'
if (Test-Path $adobe) {
    Get-ChildItem -Path $adobe -Directory -Filter 'Adobe After Effects *' -ErrorAction SilentlyContinue | ForEach-Object {
        $target = Join-Path $_.FullName 'Support Files\Plug-ins\Effects\CorridorKey.aex'
        if (Test-Path $target) {
            Remove-Item -Force $target -ErrorAction SilentlyContinue
            if (Test-Path $target) {
                Warn "Could not remove $target (file locked?)"
            } else {
                Ok "Removed: $target"
            }
        }
    }
}

# --- 3. Run the existing uninstaller if one is present ---------------------
$installRoot = Join-Path ${env:ProgramFiles} 'CorridorKey'
$uninsExe = Join-Path $installRoot 'unins000.exe'
if (Test-Path $uninsExe) {
    Say "Found existing install at $installRoot - running its uninstaller"
    # /VERYSILENT: no dialogs at all.
    # /SUPPRESSMSGBOXES: auto-answer Yes to any prompts.
    # /NORESTART: never reboot.
    Start-Process -Wait -FilePath $uninsExe -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'
    if (Test-Path $installRoot) {
        Warn "Uninstaller exited but $installRoot still exists - will force-remove below"
    } else {
        Ok "Uninstaller completed"
    }
}

# --- 4. Force-remove install tree (stragglers from a borked uninstall) -----
Say "Force-removing install tree $installRoot"
if (Test-Path $installRoot) {
    Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
    if (Test-Path $installRoot) {
        Fail "$installRoot still exists - something has an open handle. Check Task Manager for lingering python.exe or AfterFX.exe processes."
    } else {
        Ok "Removed $installRoot"
    }
} else {
    Ok "$installRoot already clean"
}

# --- 5. Remove any dev/Phase 4 tree under %LOCALAPPDATA% -------------------
# Phase 4 (pre-installer) staged everything at %LOCALAPPDATA%\CorridorKey\.
# Remove the runtime + python + plugin dirs but preserve models\ (the
# user's cached weights) unless -KeepModelCache:$false.
$localRoot = Join-Path ${env:LOCALAPPDATA} 'CorridorKey'
if (Test-Path $localRoot) {
    Say "Removing dev tree at $localRoot"
    if ($KeepModelCache -and (Test-Path (Join-Path $localRoot 'models'))) {
        # Preserve models\ by moving it aside, nuking the parent, then
        # restoring. This is crude but portable across PS versions.
        $stash = Join-Path ${env:LOCALAPPDATA} "CorridorKey-models-stash-$(Get-Random)"
        Move-Item -Force (Join-Path $localRoot 'models') $stash
        Remove-Item -Recurse -Force $localRoot -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $localRoot | Out-Null
        Move-Item -Force $stash (Join-Path $localRoot 'models')
        Ok "Dev tree removed, model cache preserved at $localRoot\models\"
    } else {
        Remove-Item -Recurse -Force $localRoot -ErrorAction SilentlyContinue
        Ok "Dev tree fully removed (including model cache)"
    }
}

# Also clean the Phase 4 backup-of-backup location the earlier hand-craft
# may have left behind.
$stashPath = Join-Path ${env:LOCALAPPDATA} 'CorridorKey-models-backup'
if (Test-Path $stashPath) {
    if ($KeepModelCache) {
        # Restore it into %LOCALAPPDATA%\CorridorKey\models\
        $modelsDst = Join-Path $localRoot 'models'
        if (-not (Test-Path $modelsDst)) {
            New-Item -ItemType Directory -Force -Path $localRoot | Out-Null
            Move-Item -Force $stashPath $modelsDst
            Ok "Restored stashed models cache -> $modelsDst"
        } else {
            Warn "Both $stashPath and $modelsDst exist. Leaving both alone."
        }
    } else {
        Remove-Item -Recurse -Force $stashPath
        Ok "Removed stashed model backup"
    }
}

# --- 6. Clean temp files ---------------------------------------------------
Remove-Item -Force -ErrorAction SilentlyContinue `
    (Join-Path ${env:TEMP} 'corridorkey_runtime.port'), `
    (Join-Path ${env:TEMP} 'corridorkey_runtime.log')
Ok "Cleaned temp files"

# --- 7. Remove stray InnoSetup registry receipts --------------------------
# If the unins000.exe above ran successfully it cleared its own reg key,
# but a borked previous uninstall can leave an orphaned "please confirm
# this app is installed" entry. Match by our AppId.
$appIdKey = '{B3D9C2A7-E5F1-4A8B-9C2D-CORRIDORKEY01}_is1'
$regPaths = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$appIdKey",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$appIdKey"
)
foreach ($rp in $regPaths) {
    if (Test-Path $rp) {
        Remove-Item -Recurse -Force $rp -ErrorAction SilentlyContinue
        Ok "Removed registry entry $rp"
    }
}

# --- 8. Warn about dev escape hatch ---------------------------------------
$envRoot = [Environment]::GetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', 'User')
if ($envRoot) {
    Warn "CORRIDORKEY_REPO_ROOT is set in your user environment ($envRoot)."
    Warn "The bridge will prefer that over the installed runtime. Clear it for a true test:"
    Warn "    [Environment]::SetEnvironmentVariable('CORRIDORKEY_REPO_ROOT', `$null, 'User')"
    Warn "Then launch a fresh AE (env vars are inherited at process start)."
}

# --- Verify clean state ----------------------------------------------------
Say "Verifying clean state"
$residual = 0
if (Test-Path $installRoot) { Warn "Still present: $installRoot"; $residual++ }
if (Test-Path $adobe) {
    Get-ChildItem -Path $adobe -Directory -Filter 'Adobe After Effects *' -ErrorAction SilentlyContinue | ForEach-Object {
        $target = Join-Path $_.FullName 'Support Files\Plug-ins\Effects\CorridorKey.aex'
        if (Test-Path $target) { Warn "Still present: $target"; $residual++ }
    }
}
foreach ($rp in $regPaths) {
    if (Test-Path $rp) { Warn "Still present: $rp"; $residual++ }
}
if ($residual -eq 0) { Ok "Clean state verified" }
else { Fail "$residual artifact(s) still present - see warnings above" }

if ($CleanOnly) {
    Write-Host ""
    Ok "Clean-only mode: done."
    exit 0
}

# --- 9. Resolve the .exe to install ----------------------------------------
if ($FromCi) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        Fail "gh CLI not found but -FromCi was requested."
    }
    if (-not $RunId) {
        Say "Finding most recent successful CI run that produced a Windows installer"
        $RunId = gh run list --repo $Repo --workflow=CI --status success --json databaseId --jq '.[0].databaseId'
        if (-not $RunId) { Fail "No successful CI runs found." }
        Ok "Using CI run $RunId"
    }
    $dlDir = Join-Path ${env:TEMP} "ck-exe-dl-$(Get-Random)"
    New-Item -ItemType Directory -Force -Path $dlDir | Out-Null
    Say "Downloading CorridorKey-Installer-Windows artifact to $dlDir"
    gh run download $RunId --repo $Repo -n CorridorKey-Installer-Windows -D $dlDir
    $ExePath = (Get-ChildItem -Path $dlDir -Filter 'CorridorKey-*.exe' | Select-Object -First 1).FullName
    if (-not $ExePath) { Fail "No .exe found inside the artifact." }
    Ok "Downloaded: $ExePath"
}

if (-not $ExePath) {
    $repoRoot = (git rev-parse --show-toplevel 2>$null)
    if (-not $repoRoot) { $repoRoot = (Get-Location).Path }
    $localExe = Get-ChildItem -Path (Join-Path $repoRoot 'dist') -Filter 'CorridorKey-*-windows-*.exe' -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($localExe) {
        $ExePath = $localExe.FullName
        Say "No -ExePath given, using most recent local build: $ExePath"
    } else {
        Fail "No .exe specified. Pass -ExePath, -FromCi, or build one with scripts\installer\build_windows.ps1"
    }
}

if (-not (Test-Path $ExePath)) { Fail "Not a file: $ExePath" }

# --- 10. Install -----------------------------------------------------------
Say "Installing $ExePath (this takes ~5 min - most of it is the PyTorch CUDA download)"
# /VERYSILENT: no UI at all
# /SUPPRESSMSGBOXES: auto-Yes any prompts
# /NORESTART: never reboot
# /LOG: dump InnoSetup's install log
$logPath = Join-Path ${env:TEMP} "CorridorKey-install.log"
Start-Process -Wait -FilePath $ExePath `
    -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=$logPath"
if (-not (Test-Path $installRoot)) {
    Fail "Installer exited but $installRoot was not created. Check $logPath"
}
Ok "Installer exited successfully (log: $logPath)"

# --- 11. Verify install ----------------------------------------------------
Say "Verifying install"
$checks = @(
    "$installRoot\python\python.exe",
    "$installRoot\runtime\.venv\Scripts\python.exe",
    "$installRoot\runtime\server\main.py",
    "$installRoot\runtime\engines\pytorch_engine.py",
    "$installRoot\plugin\CorridorKey.aex",
    "$installRoot\installer\requirements.txt",
    "$installRoot\installer\requirements-torch.txt",
    "$installRoot\VERSION"
)
foreach ($p in $checks) {
    if (Test-Path $p) { Ok "Found: $p" }
    else { Fail "Missing: $p" }
}

# Plugin landed in at least one AE install?
$pluginInstalled = $false
if (Test-Path $adobe) {
    Get-ChildItem -Path $adobe -Directory -Filter 'Adobe After Effects *' -ErrorAction SilentlyContinue | ForEach-Object {
        $target = Join-Path $_.FullName 'Support Files\Plug-ins\Effects\CorridorKey.aex'
        if (Test-Path $target) {
            Ok "Plugin installed into: $($_.FullName)"
            $pluginInstalled = $true
        }
    }
}
if (-not $pluginInstalled) {
    Warn "No AE install had CorridorKey.aex copied in - check $logPath"
}

# Runtime import sanity check (proves the venv is actually usable + CUDA).
Say "Running import sanity check inside the installed venv"
$venvPy = "$installRoot\runtime\.venv\Scripts\python.exe"
$importCheck = @"
import torch, numpy, PIL, cv2, msgpack, timm, safetensors
print('torch:', torch.__version__)
print('cuda available:', torch.cuda.is_available())
print('device:', torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'cpu')
print('imports ok')
"@
& $venvPy -c $importCheck
if ($LASTEXITCODE -ne 0) { Fail "Runtime venv import check failed" }
Ok "Runtime venv imports work"

Write-Host ""
Ok "Install verified."
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Launch After Effects"
Write-Host "  2. Apply Effect > Keying > CorridorKey to a green-screen layer"
Write-Host "  3. First frame will pause briefly while the runtime boots"
Write-Host ""
Write-Host "Logs:"
Write-Host "  $logPath   (InnoSetup install log)"
Write-Host "  ${env:TEMP}\corridorkey_runtime.log   (runtime, fresh each launch)"
