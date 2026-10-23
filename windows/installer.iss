; Inno Setup script for the Windows installer:  build\LivingMarble-<version>-setup.exe
; Build it with windows\build-installer.bat (which builds the game first), or
; open this file in Inno Setup 6 once build\Release\living_marble.exe exists.

#define AppName "Living Marble"
#define AppExe  "living_marble.exe"
#define AppExePath SourcePath + "..\build\Release\" + AppExe
#if !FileExists(AppExePath)
  #error Build the game first: build\Release\living_marble.exe is missing
#endif
; the version comes from the exe, which has it from project(VERSION) in CMakeLists.txt
#define AppVersion GetStringFileInfo(AppExePath, "ProductVersion")
#if AppVersion == ""
  #error living_marble.exe has no version info - rebuild it with CMake
#endif

[Setup]
; keep AppId the same in every version, so a new one installs over the old one
AppId={{83D075F2-6917-4CD0-95A0-F1FDCD80A60B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=David Weiser
AppPublisherURL=https://github.com/davidfweiser/LivingMarbleChess
AppSupportURL=https://github.com/davidfweiser/LivingMarbleChess/issues
AppUpdatesURL=https://github.com/davidfweiser/LivingMarbleChess/releases/latest
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} {#AppVersion} Setup
; just for you (no admin, into your own Programs folder) or for everyone (Program Files)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=AppIcon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\build
OutputBaseFilename=LivingMarble-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppExePath}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "3D battle chess"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "3D battle chess"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
