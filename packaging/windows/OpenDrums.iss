; Inno Setup script. Parameters (ISCC /D...): AppVersion, BuildDir (…/OpenDrums_artefacts/Release), RepoDir, OutDir
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6B0D7F5E-3C1A-4E0B-9A55-0D1E2F3A4B5C}
AppName=OpenDrums
AppVersion={#AppVersion}
AppPublisher=OpenDrums
DefaultDirName={autopf}\OpenDrums
DefaultGroupName=OpenDrums
OutputDir={#OutDir}
OutputBaseFilename=OpenDrums-{#AppVersion}-Windows-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=OpenDrums

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "vst3";       Description: "Plugin VST3";                  Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Aplicativo independente";      Types: full
Name: "content";    Description: "Kits e grooves de exemplo";    Types: full compact

[Files]
; VST3 is a folder bundle on Windows
Source: "{#BuildDir}\VST3\OpenDrums.vst3\*"; DestDir: "{commoncf64}\VST3\OpenDrums.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#BuildDir}\Standalone\OpenDrums.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#RepoDir}\kits\*";    DestDir: "{commonappdata}\OpenDrums\Kits";    Components: content; Flags: recursesubdirs ignoreversion
Source: "{#RepoDir}\grooves\*"; DestDir: "{commonappdata}\OpenDrums\Grooves"; Components: content; Flags: recursesubdirs ignoreversion
Source: "{#RepoDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\OpenDrums"; Filename: "{app}\OpenDrums.exe"; Components: standalone
Name: "{group}\Desinstalar OpenDrums"; Filename: "{uninstallexe}"
