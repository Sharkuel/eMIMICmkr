; Inno Setup script for the Windows installer. CI runs:
;   iscc /DAppVersion=1.0.0 /DAppExe=<path to eMIMICmkr.exe> /O<output folder> packaging\windows\eMIMICmkr.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\eMIMICmkr_artefacts\Release\eMIMICmkr.exe"
#endif

[Setup]
; Keep this AppId: Windows uses it to upgrade an installed copy instead of adding a second one.
AppId={{97B07549-7224-437C-BFAD-81E73D06B8EA}
AppName=eMIMICmkr
AppVersion={#AppVersion}
AppVerName=eMIMICmkr {#AppVersion}
AppPublisher=eMIMICmkr
AppPublisherURL=https://github.com/Sharkuel/eMIMICmkr
AppSupportURL=https://github.com/Sharkuel/eMIMICmkr/issues
DefaultDirName={autopf}\eMIMICmkr
DefaultGroupName=eMIMICmkr
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=eMIMICmkr-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Installs for everyone by default; the first page offers "only for me" without admin rights.
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\eMIMICmkr.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\eMIMICmkr"; Filename: "{app}\eMIMICmkr.exe"
Name: "{autodesktop}\eMIMICmkr"; Filename: "{app}\eMIMICmkr.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\eMIMICmkr.exe"; Description: "{cm:LaunchProgram,eMIMICmkr}"; Flags: nowait postinstall skipifsilent
