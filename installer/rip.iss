; RIP Windows installer
; Compile with:
;   ISCC.exe /DAppVersion=1.1.13 /DAppArch=x64 /DArchitectureAllowed=x64compatible /DArchitectureInstallMode=x64compatible installer\rip.iss
; Or pass arm64 for both architecture directives to build the native ARM64 installer.

#ifndef AppVersion
#define AppVersion "1.1.13"
#endif

#ifndef AppArch
#define AppArch "x64"
#endif

#ifndef ArchitectureAllowed
#define ArchitectureAllowed "x64compatible"
#endif

#ifndef ArchitectureInstallMode
#define ArchitectureInstallMode "x64compatible"
#endif

[Setup]
AppId={{A2B67086-6B31-4D6B-A7D5-0D943152818C}
AppName=RIP Archive Toolkit
SetupIconFile=..\branding\rip.ico
AppVersion={#AppVersion}
AppVerName=RIP Archive Toolkit {#AppVersion} ({#AppArch})
AppPublisher=Cooper-Src
AppPublisherURL=https://github.com/Cooper-Src/rip
AppSupportURL=https://github.com/Cooper-Src/rip/issues
AppUpdatesURL=https://github.com/Cooper-Src/rip/releases
AppReadmeFile={app}\README-Windows.txt
DefaultDirName={localappdata}\Programs\RIP
DefaultGroupName=RIP
UninstallDisplayIcon={app}\rip-gui.exe
UninstallDisplayName=RIP Archive Toolkit ({#AppArch})
PrivilegesRequired=lowest
ArchitecturesAllowed={#ArchitectureAllowed}
ArchitecturesInstallIn64BitMode={#ArchitectureInstallMode}
WizardStyle=modern
SetupLogging=yes
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\build\installer
OutputBaseFilename=rip-{#AppVersion}-windows-{#AppArch}-setup
DisableProgramGroupPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\package\rip-{#AppVersion}-windows-{#AppArch}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\RIP Archive Toolkit"; Filename: "{app}\rip-gui.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\RIP Archive Toolkit"; Filename: "{app}\rip-gui.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\rip-gui.exe"; Description: "Launch RIP Archive Toolkit"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent
