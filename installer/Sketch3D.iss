; Inno Setup script for Sketch3D
; Build: run build_installer.ps1 (Release build + windeployqt + ISCC)

#define AppName "Sketch3D"
#define AppVersion "3.5.0"
#define AppExe "Sketch3D.exe"
#define DeployDir "..\deploy\Sketch3D"

[Setup]
AppId={{8C5B2F4E-3D7A-4E1B-9A2C-5F6D3E8B1A47}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile=..\resources\Sketch3D.ico
OutputDir=..\dist
OutputBaseFilename=Sketch3D_Setup_{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "assocsk3"; Description: "Open .sk3 files with {#AppName}"; GroupDescription: "File association:"

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.sk3"; ValueType: string; ValueName: ""; ValueData: "Sketch3D.Drawing"; Flags: uninsdeletevalue; Tasks: assocsk3
Root: HKA; Subkey: "Software\Classes\Sketch3D.Drawing"; ValueType: string; ValueName: ""; ValueData: "Sketch3D Drawing"; Flags: uninsdeletekey; Tasks: assocsk3
Root: HKA; Subkey: "Software\Classes\Sketch3D.Drawing\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assocsk3
Root: HKA; Subkey: "Software\Classes\Sketch3D.Drawing\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assocsk3

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
