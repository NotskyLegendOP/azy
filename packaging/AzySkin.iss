; Azy Skin - Inno Setup script (Inno Setup 6.x)
;
;   iscc packaging\AzySkin.iss            (or use scripts\package.ps1)
;
; Guarantees required by the design:
;   * Azy installs into its own directory ({autopf}\Azy Skin) and never writes
;     into an Adobe directory.
;   * Nothing about Premiere Pro is installed, patched, replaced or registered.
;   * Uninstalling removes Azy's files, its settings, its log and its
;     "Start with Windows" entry - and nothing else.

#define AppName "Azy Skin"
#define AppVersion "1.0.0"
#define AppPublisher "Azy Skin"
#define AppExeName "AzySkin.exe"

[Setup]
AppId={{9D3B1C2E-6F41-4A57-9E8C-2B7A5C4D1E10}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install: Azy needs no administrator rights, and running it elevated
; would stop it from observing a normally-launched Premiere Pro. The same
; choice keeps the uninstaller able to clean up the user's settings.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist
OutputBaseFilename=AzySkin-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
; Dark, to match the application itself (6.6+; older compilers get the light wizard).
#if VER >= EncodeVer(6,6,0)
WizardStyle=modern dark
#else
WizardStyle=modern
#endif
; Refuse to run while Azy is running: the installer asks it to close instead of
; overwriting a running executable, and restarts it afterwards if it was running.
AppMutex=AzySkin.SingleInstance.7f2a1c94
CloseApplications=yes
RestartApplications=yes
UninstallDisplayIcon={app}\{#AppExeName}
; Version-guarded so the script still compiles with an older Inno Setup: the
; *compatible architecture values need 6.3, dark mode needs 6.6. Both fall back to
; something an older compiler accepts instead of failing.
#if VER >= EncodeVer(6,3,0)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif
MinVersion=10.0.17763
LicenseFile=..\LICENSE
; Relative to this script's own directory (not the repository root), which is
; how Inno resolves it - hence no packaging\ prefix.
InfoBeforeFile=info-before.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start Azy Skin with Windows (recommended)"; GroupDescription: "Startup:"
Name: "autoskin"; Description: "Automatically apply the skin to Premiere Pro"; GroupDescription: "Premiere Pro:"

[Files]
Source: "..\build\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName} Settings"; Filename: "{app}\{#AppExeName}"; Parameters: "--settings"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

; The Run entry is owned by the application (it also sets it from the tray), so
; the uninstaller only ever removes the value - it never needs to add it here.
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "Azy Skin"; Flags: dontcreatekey uninsdeletevalue

; Setup hands its two choices to the application through a one-shot file; Azy
; applies them on first launch and deletes the file, so a later user change is
; never overwritten by a re-install.
[INI]
Filename: "{localappdata}\Azy Skin\install-defaults.ini"; Section: "skin"; \
    Key: "enabled"; StringValue: "1"
Filename: "{localappdata}\Azy Skin\install-defaults.ini"; Section: "skin"; \
    Key: "start_with_windows"; StringValue: "1"; Tasks: autostart
Filename: "{localappdata}\Azy Skin\install-defaults.ini"; Section: "skin"; \
    Key: "start_with_windows"; StringValue: "0"; Tasks: not autostart
Filename: "{localappdata}\Azy Skin\install-defaults.ini"; Section: "skin"; \
    Key: "apply_automatically"; StringValue: "1"; Tasks: autoskin
Filename: "{localappdata}\Azy Skin\install-defaults.ini"; Section: "skin"; \
    Key: "apply_automatically"; StringValue: "0"; Tasks: not autoskin

[Run]
Filename: "{app}\{#AppExeName}"; Parameters: "--tray"; \
    Description: "Start {#AppName} now (it lives in the system tray)"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Azy's own data only: configuration, log (and its rotated copy).
Type: filesandordirs; Name: "{localappdata}\Azy Skin"
