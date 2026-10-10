; Inno Setup 6/7 - Pacote Windows x64 LevelCAD
; Salve este arquivo em: LevelCAD\packaging\LevelCAD.iss
#define AppVersion "1.1.0-Cinnamon"

[Setup]
AppId={{6E788E96-7F72-4757-B6B8-A51C9AA97324}
AppName=LevelCAD
AppVersion={#AppVersion}
AppVerName=LevelCAD {#AppVersion}
AppPublisher=The ArchLevel Foundation
AppPublisherURL=https://github.com/welabsdev
DefaultDirName={autopf}\LevelCAD
DefaultGroupName=LevelCAD
UninstallDisplayIcon={app}\LevelCAD.exe
OutputDir=..\dist\setup
OutputBaseFilename=LevelCAD-Setup-{#AppVersion}-x64
SetupIconFile=LevelCAD.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\LICENSE

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na area de trabalho"; Flags: unchecked

[Files]
Source: "..\dist\LevelCAD\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe,*.pdb,*.ilk"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\LevelCAD\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\LevelCAD"; Filename: "{app}\LevelCAD.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\LevelCAD"; Filename: "{app}\LevelCAD.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Instalando Microsoft Visual C++ Runtime..."; Flags: runhidden waituntilterminated; Check: NeedsVCRuntime
Filename: "{app}\LevelCAD.exe"; Description: "Abrir LevelCAD"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVCRuntime(): Boolean;
var
  Installed, MajorVersion, MinorVersion: Cardinal;
  RuntimeKey: String;
begin
  RuntimeKey := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Installed', Installed) then begin
    Result := True;
    Exit;
  end;
  if Installed <> 1 then begin
    Result := True;
    Exit;
  end;
  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Major', MajorVersion) then begin
    Result := True;
    Exit;
  end;
  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Minor', MinorVersion) then begin
    Result := True;
    Exit;
  end;
  // Dependencia estimada para LevelCAD compilado com MSVC 14.51 (Visual Studio 2026).
  Result := (MajorVersion < 14) or ((MajorVersion = 14) and (MinorVersion < 51));
end;
