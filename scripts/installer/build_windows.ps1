<#
.SYNOPSIS
    Build the Windows .exe installer for CorridorKey AE via InnoSetup 6.

.DESCRIPTION
    Mirror of scripts/installer/build_macos.sh. Stages the install payload
    into a temp tree (embedded Python + runtime source + plugin binary +
    requirements files + VERSION), then invokes ISCC on
    installer/windows/CorridorKey.iss to produce

        dist/CorridorKey-<version>-windows-x64.exe

    Works unchanged on the GitHub Actions windows-latest runner and on
    a local dev machine. No CI-specific logic lives in here.

.PARAMETER Version
    Version tag baked into the installer filename + AppVersion.
    Defaults to 0.1.0.

.PARAMETER PluginAex
    Path to the built CorridorKey.aex. Defaults to
    build_win\plugin\Release\CorridorKey.aex. Build with
        cmake --build build_win --config Release
    first, or pass -PluginAex to override.

.PARAMETER OutDir
    Where the final .exe lands. Defaults to dist\.

.PARAMETER Iscc
    Path to ISCC.exe. Auto-detected from the standard InnoSetup 6
    install locations; pass -Iscc to override.

.EXAMPLE
    # CI default - picks up build_win\plugin\Release\CorridorKey.aex
    pwsh scripts\installer\build_windows.ps1 -Version 0.1.0

.EXAMPLE
    # Point at a specific plugin copy
    pwsh scripts\installer\build_windows.ps1 `
        -Version 0.1.0-abc1234 `
        -PluginAex D:\artifacts\CorridorKey.aex
#>

[CmdletBinding()]
param(
    [string]$Version   = "0.1.0",
    [string]$PluginAex = "",
    [string]$OutDir    = "",
    [string]$Iscc      = ""
)

$ErrorActionPreference = "Stop"

# python-build-standalone release tag + CPython version. Kept in sync with
# scripts/installer/build_macos.sh so both platforms stay on the same
# Python (currently 3.12.13).
$PbsTag     = "20260414"
$PbsVersion = "3.12.13"
$PbsTarball = "cpython-$PbsVersion+$PbsTag-x86_64-pc-windows-msvc-install_only.tar.gz"
$PbsUrl     = "https://github.com/astral-sh/python-build-standalone/releases/download/$PbsTag/$PbsTarball"

# --- Resolve paths ---
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir '..\..')

if (-not $PluginAex) {
    $PluginAex = Join-Path $RepoRoot 'build_win\plugin\Release\CorridorKey.aex'
}
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot 'dist'
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

if (-not (Test-Path $PluginAex)) {
    Write-Error "Plugin not found at $PluginAex. Build it first: cmake --build build_win --config Release"
}

# --- Locate ISCC ---
if (-not $Iscc) {
    $candidates = @(
        "$Env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$Env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${Env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { $Iscc = $c; break }
    }
}
if (-not $Iscc -or -not (Test-Path $Iscc)) {
    Write-Error @"
ISCC.exe not found. Install InnoSetup 6:
    winget install --id JRSoftware.InnoSetup -e
or pass -Iscc to point at it explicitly.
"@
}

# --- Workspace ---
$Work        = Join-Path $RepoRoot 'build\installer-windows'
$Staging     = Join-Path $Work 'staging'
$CacheDir    = Join-Path $RepoRoot '.build-cache'
$PbsCache    = Join-Path $CacheDir $PbsTarball

New-Item -ItemType Directory -Force -Path $Work, $Staging, $CacheDir, $OutDir | Out-Null

Write-Host "=== CorridorKey AE installer builder (Windows) ===" -ForegroundColor Cyan
Write-Host "Version:      $Version"
Write-Host "Plugin:       $PluginAex"
Write-Host "Staging:      $Staging"
Write-Host "ISCC:         $Iscc"
Write-Host "Output:       $OutDir"
Write-Host ""

# --- Clean staging so we don't carry state across rebuilds ---
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Force -Path $Staging | Out-Null

# --- 1. Download python-build-standalone ---
if (-not (Test-Path $PbsCache)) {
    Write-Host "Downloading $PbsTarball ..."
    $tmp = "$PbsCache.tmp"
    # Invoke-WebRequest is ~2x slower than curl on big downloads; prefer
    # curl.exe which ships with Win10+.
    curl.exe -sSL -o $tmp $PbsUrl
    if ($LASTEXITCODE -ne 0) { Write-Error "curl failed fetching $PbsUrl" }
    Move-Item -Force $tmp $PbsCache
}
$sizeMb = [int]((Get-Item $PbsCache).Length / 1MB)
Write-Host "Python tarball: $PbsCache (${sizeMb} MB)"

# --- 2. Extract python into the staging tree ---
# Windows 10 1803+ ships a BSD-style tar at C:\Windows\System32\tar.exe
# that handles .tar.gz natively. Invoke it by full path to avoid picking
# up GNU tar from a Git Bash / MSYS PATH - GNU tar parses "C:\..." as
# a remote "host:path" spec and fails with "Cannot connect to C".
$WinTar = Join-Path $Env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path $WinTar)) {
    Write-Error "Windows tar.exe not found at $WinTar - requires Win10 1803+"
}
Write-Host "Extracting python into staging..."
& $WinTar -xzf $PbsCache -C $Staging
if ($LASTEXITCODE -ne 0) { Write-Error "tar.exe failed extracting $PbsCache" }

# pbs extracts to "python/" at the staging root - verify
$PbsPython = Join-Path $Staging 'python\python.exe'
if (-not (Test-Path $PbsPython)) {
    Write-Error "python\python.exe not found after extract - check the PBS tarball layout"
}
Write-Host "Embedded Python: $PbsPython"

# --- 3. Copy runtime source into staging ---
Write-Host "Copying runtime source..."
$RuntimeDst = Join-Path $Staging 'runtime'
New-Item -ItemType Directory -Force -Path $RuntimeDst | Out-Null

function Copy-RuntimeSubdir([string]$Name) {
    $src = Join-Path $RepoRoot "runtime\$Name"
    $dst = Join-Path $RuntimeDst $Name
    if (-not (Test-Path $src)) {
        Write-Error "Expected runtime source not found: $src"
    }
    # Robocopy is much faster than Copy-Item for tree copies and supports
    # exclusions cleanly. Exit codes 0-7 are all success variants.
    robocopy $src $dst /E /NFL /NDL /NJH /NJS /NP /XD '__pycache__' '.venv' 'tests' | Out-Null
    if ($LASTEXITCODE -ge 8) { Write-Error "robocopy failed copying $src -> $dst" }
}
Copy-RuntimeSubdir 'server'
Copy-RuntimeSubdir 'engines'
Copy-RuntimeSubdir 'models'
Copy-Item (Join-Path $RepoRoot 'runtime\pyproject.toml') $RuntimeDst -Force

# robocopy sets $LASTEXITCODE to 1 (one file copied, fine) on success,
# which confuses later commands. Reset.
$global:LASTEXITCODE = 0

# --- 4. Write VERSION file ---
Set-Content -Path (Join-Path $Staging 'VERSION') -Value $Version -NoNewline

# --- 5. Invoke ISCC ---
# /DStagingDir and /DPluginAex tell the .iss where the staged tree and
# .aex live; they're the only paths not hardcoded in the script.
$IssPath = Join-Path $RepoRoot 'installer\windows\CorridorKey.iss'
if (-not (Test-Path $IssPath)) {
    Write-Error "InnoSetup script not found: $IssPath"
}

Write-Host "Running ISCC..."
& $Iscc `
    "/DAppVersion=$Version" `
    "/DStagingDir=$Staging" `
    "/DPluginAex=$PluginAex" `
    "/O$OutDir" `
    $IssPath

if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC failed with exit code $LASTEXITCODE"
}

# --- 6. Report ---
$FinalExe = Get-ChildItem -Path $OutDir -Filter "CorridorKey-$Version-*.exe" |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $FinalExe) {
    Write-Error "Could not find installer .exe in $OutDir after ISCC"
}

$finalSizeMb = [int]($FinalExe.Length / 1MB)
Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "Installer: $($FinalExe.FullName)"
Write-Host "Size:      $finalSizeMb MB"
Write-Host ""
Write-Host "To test locally:"
Write-Host "  Start-Process -Wait -FilePath `"$($FinalExe.FullName)`""
Write-Host "(The installer requires admin - will prompt for UAC.)"
