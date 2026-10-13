; Script Inno Setup pour Block Launcher.
; Compilation : "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\BlockLauncher.iss
; Pré-requis : le dossier ..\dist doit contenir BlockLauncher.exe + toutes les DLL Qt
; (générées via windeployqt, voir installer\build_installer.ps1).

#define MyAppName "Block Launcher"
#define MyAppVersion "1.2.0"
#define MyAppPublisher "Block Launcher"
#define MyAppExeName "BlockLauncher.exe"
#define DistDir "..\dist"
; ID client Azure préconfiguré (facultatif) : laisse "" pour que le champ reste vide à
; l'installation. Modifie cette valeur avant de recompiler si tu changes d'application Azure.
#define DefaultClientId "1e2c3b67-0d62-497f-b7e2-4d8630876a72"

[Setup]
AppId={{B9B9C8C0-6C2E-4C7E-9B7B-5B6C9B6E7A11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=BlockLauncherSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\src\icon.ico

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer une icône sur le Bureau"; GroupDescription: "Icônes supplémentaires :"

[Files]
Source: "{#DistDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs
Source: "{#DistDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Lancer {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\client_id.txt"

; --- Page personnalisée : ID client Azure (facultatif) -----------------------------------
; Permet de préconfigurer la connexion Microsoft sans que l'utilisateur n'ait à éditer les
; paramètres après coup. Voir src/Config.h : le launcher lit {app}\client_id.txt au démarrage.
[Code]
var
  ClientIdPage: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  ClientIdPage := CreateInputQueryPage(wpSelectDir,
    'ID client Azure (facultatif)',
    'Nécessaire pour la connexion avec un compte Microsoft',
    'Si tu as créé ta propre application Microsoft Entra pour Block Launcher, colle son ID ' +
    'client ci-dessous. Tu peux laisser ce champ vide et le renseigner plus tard dans ' +
    'Paramètres.');
  ClientIdPage.Add('ID client (GUID) :', False);
  ClientIdPage.Values[0] := '{#DefaultClientId}';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ClientId: String;
begin
  if CurStep = ssPostInstall then
  begin
    ClientId := Trim(ClientIdPage.Values[0]);
    if ClientId <> '' then
      SaveStringToFile(ExpandConstant('{app}\client_id.txt'), ClientId, False);
  end;
end;
