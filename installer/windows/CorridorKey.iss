; CorridorKey AE — Windows installer (InnoSetup 6)
;
; Mirror of the macOS .pkg approach on the Mac side: ship an embedded
; python-build-standalone tree + runtime source + the .aex, then create
; the venv and pip-install deps on the user's machine at install time.
; We do NOT ship a pre-built venv — venvs bake absolute paths into
; pyvenv.cfg and shebangs, so they don't survive the trip from the
; build runner to the user's machine.
;
; Layout produced:
;
;   C:\Program Files\CorridorKey\
;   ├── python\                     <- python-build-standalone 3.12 (bundled)
;   │   └── python.exe
;   ├── runtime\
;   │   ├── .venv\                  <- created at install time
;   │   ├── server\                 <- bundled
;   │   ├── engines\                <- bundled
;   │   ├── models\                 <- bundled
;   │   └── pyproject.toml          <- bundled
;   ├── plugin\
;   │   └── CorridorKey.aex         <- bundled, also copied into AE's Plug-ins folder
;   ├── installer\
;   │   ├── requirements.txt        <- bundled, read by pip at install time
;   │   └── requirements-torch.txt  <- bundled, CUDA wheels
;   └── VERSION
;
; And the .aex is dropped into the highest detected AE install under
;   C:\Program Files\Adobe\Adobe After Effects <ver>\Support Files\Plug-ins\Effects\
;
; Both the install root and the AE plug-ins folder are under Program Files,
; so PrivilegesRequired=admin is unavoidable. Plugin Loading.log confirms
; AE Windows only scans paths under C:\Program Files\Adobe\... — no
; per-user plug-ins folder gets scanned, so a no-UAC install isn't
; possible on Windows just like it isn't on macOS.
;
; Compile with:
;   "C:\Users\<user>\AppData\Local\Programs\Inno Setup 6\ISCC.exe" \
;       /DAppVersion=0.1.0 \
;       /DStagingDir=C:\path\to\staging \
;       /DPluginAex=C:\path\to\CorridorKey.aex \
;       installer\windows\CorridorKey.iss
;
; The orchestrator is scripts\installer\build_windows.ps1 — it stages
; the tree (downloads pbs, copies runtime source, drops the .aex) and
; invokes ISCC with the right /D vars.

#define AppName       "CorridorKey"
#define AppPublisher  "Josh Davies / Corridor Digital"
#define AppURL        "https://github.com/iamjoshuadavies/corridorkey-ae"
#define AppExeName    "CorridorKey.aex"

; These three are passed in by build_windows.ps1. Fallbacks let the
; script be opened in the InnoSetup IDE for inspection without
; build_windows.ps1's context.
#ifndef AppVersion
  #define AppVersion "0.1.0-dev"
#endif
#ifndef StagingDir
  #define StagingDir "..\..\build\installer-windows\staging"
#endif
#ifndef PluginAex
  #define PluginAex "..\..\build_win\plugin\Release\CorridorKey.aex"
#endif

[Setup]
AppId={{B3D9C2A7-E5F1-4A8B-9C2D-CORRIDORKEY01}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}

; Install everything under Program Files\CorridorKey. We need admin
; anyway to drop the .aex into AE's plug-ins folder, so machine-wide
; install is the cleaner shape — matches macOS's
; /Library/Application Support/CorridorKey/ semantics.
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=

; 64-bit only. CorridorKey requires CUDA / RTX which implies x64.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Output
OutputDir=..\..\dist
OutputBaseFilename=CorridorKey-{#AppVersion}-windows-x64
Compression=lzma2/ultra64
SolidCompression=yes

; UI
WizardStyle=modern
WizardSizePercent=110
ShowLanguageDialog=no
AllowNoIcons=yes
UninstallDisplayName={#AppName} {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; --- Embedded Python (python-build-standalone 3.12 x86_64-pc-windows-msvc install_only) ---
; Staged tree gets recursively bundled. ~45 MB extracted, ~15 MB in the
; .exe after InnoSetup's LZMA compression.
Source: "{#StagingDir}\python\*"; DestDir: "{app}\python"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; --- Runtime source (server/, engines/, models/, pyproject.toml) ---
; __pycache__ directories are filtered out at staging time in
; build_windows.ps1 so we don't end up with .pyc files keyed to the
; build machine's Python.
Source: "{#StagingDir}\runtime\*"; DestDir: "{app}\runtime"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; --- Plugin binary ---
; The postinstall step copies this into the detected AE Plug-ins folder.
Source: "{#PluginAex}"; DestDir: "{app}\plugin"; \
    DestName: "CorridorKey.aex"; Flags: ignoreversion

; --- Requirements files (pip reads these at install time) ---
Source: "{#SourcePath}\requirements.txt";        DestDir: "{app}\installer"; \
    Flags: ignoreversion
Source: "{#SourcePath}\requirements-torch.txt";  DestDir: "{app}\installer"; \
    Flags: ignoreversion

; --- VERSION file for upgrade detection ---
Source: "{#StagingDir}\VERSION"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; Step 1: Create the venv in place from the embedded python.
; The embedded interpreter lives at a stable path ({app}\python) so the
; venv's symlinks / launcher references stay valid for the life of the
; install. Doing this at install time (not build time) avoids the abspath
; bake-in problem that bit the macOS prototype.
Filename: "{app}\python\python.exe"; \
    Parameters: "-m venv ""{app}\runtime\.venv"""; \
    StatusMsg: "Creating Python virtual environment..."; \
    Flags: runhidden waituntilterminated

; Step 2: Install the CPU-side deps (msgpack, numpy, Pillow, cv2, timm, safetensors).
; ~30 seconds on a fast connection.
Filename: "{app}\runtime\.venv\Scripts\python.exe"; \
    Parameters: "-m pip install --no-cache-dir --disable-pip-version-check -r ""{app}\installer\requirements.txt"""; \
    StatusMsg: "Installing Python dependencies (1/2)..."; \
    Flags: runhidden waituntilterminated

; Step 3: Install the CUDA PyTorch wheels (~2 GB download on fast networks).
; This is by far the slowest step — on a ~50 Mbps line it's 5 minutes.
Filename: "{app}\runtime\.venv\Scripts\python.exe"; \
    Parameters: "-m pip install --no-cache-dir --disable-pip-version-check -r ""{app}\installer\requirements-torch.txt"""; \
    StatusMsg: "Installing PyTorch CUDA (this is slow — ~2 GB download)..."; \
    Flags: runhidden waituntilterminated

[Code]
{
    The postinstall logic that's NOT a simple file copy lives here:
      - AE version detection by enumerating
        "C:\Program Files\Adobe\Adobe After Effects *"
      - Copying the .aex into the highest detected version's
        Plug-ins\Effects folder
      - Removing the .aex from every AE install on uninstall
    This runs elevated (PrivilegesRequired=admin), so writing into
    Program Files\Adobe\Adobe After Effects <ver>\Support Files\Plug-ins\
    just works.
}

function FindHighestAEPluginsDir(): string;
var
    FindRec: TFindRec;
    Base, Candidate, NameOnly, VerStr: string;
    i, VerNum, Best: Integer;
    BestDir: string;
begin
    Result := '';
    BestDir := '';
    Best := 0;
    Base := ExpandConstant('{autopf}') + '\Adobe';
    if FindFirst(Base + '\Adobe After Effects *', FindRec) then
    begin
        try
            repeat
                if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
                begin
                    NameOnly := FindRec.Name;
                    { Extract the trailing integer. Matches the bash idiom
                      in the macOS postinstall: `echo $name | grep -oE '[0-9]+' | tail -1`. }
                    VerStr := '';
                    for i := Length(NameOnly) downto 1 do
                    begin
                        if (NameOnly[i] >= '0') and (NameOnly[i] <= '9') then
                            VerStr := NameOnly[i] + VerStr
                        else if VerStr <> '' then
                            Break;
                    end;
                    if VerStr <> '' then
                    begin
                        VerNum := StrToIntDef(VerStr, 0);
                        Candidate := Base + '\' + NameOnly + '\Support Files\Plug-ins\Effects';
                        if (VerNum > Best) and DirExists(Candidate) then
                        begin
                            Best := VerNum;
                            BestDir := Candidate;
                        end;
                    end;
                end;
            until not FindNext(FindRec);
        finally
            FindClose(FindRec);
        end;
    end;
    Result := BestDir;
end;

procedure CopyPluginToAE();
var
    Src, Dst, AEDir: string;
begin
    AEDir := FindHighestAEPluginsDir();
    if AEDir = '' then
    begin
        MsgBox('After Effects does not appear to be installed under Program Files\Adobe. ' +
               'The CorridorKey runtime has still been installed to ' + ExpandConstant('{app}') + '. ' +
               'Install After Effects, then re-run this installer to drop the plugin into AE.',
               mbInformation, MB_OK);
        Exit;
    end;
    Src := ExpandConstant('{app}\plugin\CorridorKey.aex');
    Dst := AEDir + '\CorridorKey.aex';
    { CopyFile(src, dst, failIfExists). failIfExists=false overwrites cleanly. }
    if not CopyFile(Src, Dst, False) then
    begin
        MsgBox('Could not copy CorridorKey.aex into ' + AEDir + '. ' +
               'Make sure After Effects is not running and try the installer again.',
               mbError, MB_OK);
    end;
end;

procedure RemovePluginFromAllAEInstalls();
var
    FindRec: TFindRec;
    Base, Dst: string;
begin
    Base := ExpandConstant('{autopf}') + '\Adobe';
    if FindFirst(Base + '\Adobe After Effects *', FindRec) then
    begin
        try
            repeat
                if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
                begin
                    Dst := Base + '\' + FindRec.Name + '\Support Files\Plug-ins\Effects\CorridorKey.aex';
                    if FileExists(Dst) then
                        DeleteFile(Dst);
                end;
            until not FindNext(FindRec);
        finally
            FindClose(FindRec);
        end;
    end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
    if CurStep = ssPostInstall then
    begin
        CopyPluginToAE();
    end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
    if CurUninstallStep = usUninstall then
    begin
        RemovePluginFromAllAEInstalls();
    end;
end;

[UninstallDelete]
; The install-time-created venv isn't tracked by [Files] because it
; didn't exist at package time, so InnoSetup doesn't know to remove it
; on uninstall. Tell it to nuke the whole install dir.
Type: filesandordirs; Name: "{app}\runtime\.venv"
Type: filesandordirs; Name: "{app}"
