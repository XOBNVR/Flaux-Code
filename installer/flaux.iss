; Flaux installer for Windows (Inno Setup 6).
;
; Built automatically by .github/workflows/windows.yml, which first runs
; installer/fetch-windows-parts.ps1 to put everything in build\app:
;   build\app\flaux, flauxc\, examples\, docs\, assets\   Flaux itself
;   build\app\bin\flaux.exe                                the `flaux` command
;   build\app\python\                                      Python (embeddable)
;   build\app\toolchain\                                   gcc for Windows (w64devkit)
;   build\app\sdl2\                                        SDL2 (graphics)
;
; It installs just for the current user, so it never needs an administrator.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

[Setup]
AppId={{F6627A6B-75EF-49E8-8509-80B228128839}
AppName=Flaux
AppVersion={#MyAppVersion}
AppVerName=Flaux {#MyAppVersion}
AppPublisher=Flaux
AppPublisherURL=https://github.com/XOBNVR/Flaux-Code
AppSupportURL=https://github.com/XOBNVR/Flaux-Code
DefaultDirName={localappdata}\Programs\Flaux
DefaultGroupName=Flaux
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=FlauxSetup
SetupIconFile=..\assets\flaux.ico
UninstallDisplayIcon={app}\assets\flaux.ico
UninstallDisplayName=Flaux
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
ChangesAssociations=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Messages]
WelcomeLabel2=This will install Flaux on your computer.%n%nFlaux is a coding language that reads like English and runs super fast. You'll get Flaux Studio, an easy place to write and run your programs, plus lessons and example games.%n%nEverything Flaux needs is included. Nothing else to download.

[Tasks]
Name: "desktopicon"; Description: "Put a Flaux Studio icon on my Desktop"; GroupDescription: "Shortcuts:"
Name: "addtopath"; Description: "Let me type ""flaux"" in Command Prompt and in batch files (.bat)"; GroupDescription: "Extras:"
Name: "associate"; Description: "Open .flx files in Flaux Studio when I double-click them"; GroupDescription: "Extras:"

[InstallDelete]
; old cached copies of the compiler's own files
Type: filesandordirs; Name: "{app}\flauxc\__pycache__"

[Files]
Source: "..\build\app\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Flaux Studio"; Filename: "{app}\python\pythonw.exe"; Parameters: """{app}\flaux"" studio"; WorkingDir: "{app}"; IconFilename: "{app}\assets\flaux.ico"; Comment: "Write and run Flaux programs"
Name: "{autodesktop}\Flaux Studio"; Filename: "{app}\python\pythonw.exe"; Parameters: """{app}\flaux"" studio"; WorkingDir: "{app}"; IconFilename: "{app}\assets\flaux.ico"; Comment: "Write and run Flaux programs"; Tasks: desktopicon
Name: "{autoprograms}\Flaux lessons and guide"; Filename: "{app}\README.md"

[Registry]
; the `flaux` command, for Command Prompt and batch files
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\bin"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}\bin'))
; double-clicking a .flx file opens it in Flaux Studio
Root: HKCU; Subkey: "Software\Classes\.flx"; ValueType: string; ValueName: ""; ValueData: "Flaux.Program"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Flaux.Program"; ValueType: string; ValueName: ""; ValueData: "Flaux program"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Flaux.Program\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\assets\flaux.ico"; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Flaux.Program\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\python\pythonw.exe"" ""{app}\flaux"" studio ""%1"""; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Flaux.Program\shell\run"; ValueType: string; ValueName: ""; ValueData: "Run with Flaux"; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Flaux.Program\shell\run\command"; ValueType: string; ValueName: ""; ValueData: "cmd.exe /c """"{app}\bin\flaux.exe"" ""%1"" & pause"""; Tasks: associate

[Run]
Filename: "{app}\python\pythonw.exe"; Parameters: """{app}\flaux"" studio"; WorkingDir: "{app}"; Description: "Open Flaux Studio now"; Flags: postinstall nowait skipifsilent

[Code]
function NeedsAddPath(Folder: string): Boolean;
var
  Paths: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Folder) + ';', ';' + Uppercase(Paths) + ';') = 0;
end;

procedure RemoveFromPath(Folder: string);
var
  Paths: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths) then
    exit;
  P := Pos(';' + Uppercase(Folder) + ';', ';' + Uppercase(Paths) + ';');
  if P = 0 then
    exit;
  if P = 1 then
    Delete(Paths, 1, Length(Folder) + 1)
  else
    Delete(Paths, P - 1, Length(Folder) + 1);
  RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
