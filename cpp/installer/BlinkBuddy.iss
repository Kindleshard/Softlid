; The BlinkBuddy installer (Inno Setup 6). Built by release.ps1, which passes the version: /DAppVersion=1.0.0
; Installs for the current user only (no administrator rights), into %LocalAppData%\Programs\BlinkBuddy, which also
; lets the built-in updater replace the exe in place. Settings in %AppData%\BlinkBuddy are kept on uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{228B5231-87C7-48D5-A903-D91B6DA5D7DD}
AppName=BlinkBuddy
AppVersion={#AppVersion}
AppVerName=BlinkBuddy {#AppVersion}
AppPublisher=BlinkBuddy
DefaultDirName={localappdata}\Programs\BlinkBuddy
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=BlinkBuddy-{#AppVersion}-setup
SetupIconFile=..\res\BlinkBuddy.ico
UninstallDisplayIcon={app}\BlinkBuddy.exe
UninstallDisplayName=BlinkBuddy
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A running BlinkBuddy holds this; setup and uninstall ask for it to be closed first.
AppMutex=Local\BlinkBuddy.Instance
CloseApplications=yes

[Tasks]
Name: "startup"; Description: "Start BlinkBuddy when I sign in"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\bin\BlinkBuddy.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\res\voice\*"; DestDir: "{app}\voice"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Clips from before each language had a folder of its own.
Type: files; Name: "{app}\voice\*.mp3"

[Icons]
Name: "{autoprograms}\BlinkBuddy"; Filename: "{app}\BlinkBuddy.exe"
Name: "{autodesktop}\BlinkBuddy"; Filename: "{app}\BlinkBuddy.exe"; Tasks: desktopicon

[Registry]
; The same entry BlinkBuddy's own "Start with Windows" option writes.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "BlinkBuddy"; \
    ValueData: """{app}\BlinkBuddy.exe"" --tray"; Tasks: startup

[Run]
Filename: "{app}\BlinkBuddy.exe"; Description: "Start BlinkBuddy"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Left behind by an update or the build (the old exe steps aside as .old).
Type: files; Name: "{app}\BlinkBuddy.exe.old"
Type: files; Name: "{app}\BlinkBuddy.exe.new"

[Code]
// The start-with-Windows entry goes too, whether this installer or BlinkBuddy's own option made it.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'BlinkBuddy');
end;
