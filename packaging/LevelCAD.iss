#define AppVersion "26.10.1821-TicTac"

#ifndef StageDir
  ; Compilacao pela interface usa a distribuicao local ja preparada.
  #define StageDir SourcePath + "..\dist\LevelCAD"
#endif

#if !FileExists(StageDir + "\LevelCAD.exe")
  #error Distribuicao ausente. Execute packaging\build-setup.bat primeiro.
#endif
#if !FileExists(StageDir + "\vc_redist.x64.exe")
  #error Runtime ausente. Execute packaging\build-setup.bat primeiro.
#endif

[Setup]
AppId={{6E788E96-7F72-4757-B6B8-A51C9AA97324}
AppName=LevelCAD
AppVersion={#AppVersion}
AppVerName=LevelCAD {#AppVersion} - Stable
AppPublisher=The Archlevel Foundation
AppComments=Criado por Wenderson Dias. Publicado pela The Archlevel Foundation.

DefaultDirName={autopf}\LevelCAD
DefaultGroupName=LevelCAD
UninstallDisplayIcon={app}\LevelCAD.exe

OutputDir=..\dist\setup
OutputBaseFilename=LevelCAD-Setup-{#AppVersion}-x64
SetupIconFile=LevelCAD.ico
LicenseFile=..\LICENSE

WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

Compression=lzma2
SolidCompression=yes

; Bibliotecas grandes podem precisar de varios arquivos de instalacao.
DiskSpanning=yes
DiskSliceSize=2000000000
SlicesPerDisk=1

CloseApplications=yes
RestartApplications=no

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na area de trabalho"; Flags: unchecked
Name: "servericon"; Description: "Criar atalho para o servidor colaborativo"; Flags: unchecked

[Files]
; Executaveis, DLLs e plugins Qt preparados pelo windeployqt.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe,*.pdb,*.ilk"; Flags: ignoreversion recursesubdirs createallsubdirs

; Inclui simbolos, footprints, STEP originais, malhas e licencas.
; O historico Git nao e necessario para executar o programa.
Source: "..\libraries\*"; DestDir: "{app}\libraries"; Excludes: ".git,.git\*,.github,.github\*"; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "..\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\plugins\examples\*"; DestDir: "{app}\plugins\examples"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

Source: "{#StageDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\LevelCAD"; Filename: "{app}\LevelCAD.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\LevelCAD"; Filename: "{app}\LevelCAD.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{group}\LevelCAD Server"; Filename: "{app}\LevelCADServer.exe"; WorkingDir: "{app}"; Tasks: servericon

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
  Result := True;

  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Installed', Installed) then
    Exit;

  if Installed <> 1 then
    Exit;

  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Major', MajorVersion) then
    Exit;

  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Minor', MinorVersion) then
    Exit;

  Result := (MajorVersion < 14) or
    ((MajorVersion = 14) and (MinorVersion < 51));
end;
