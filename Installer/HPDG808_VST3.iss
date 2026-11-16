; HPDG 808: the bass instrument on its own, Windows x64, VST3 and Standalone.
; Built by Installer\Build-HPDG808.ps1 (the version comes from CMakeLists.txt, HPDG808_VERSION).
#ifndef MyVersionTag
  #error Build the installer with Installer\Build-HPDG808.ps1
#endif
#ifndef SourceVst3Dir
#define SourceVst3Dir "..\build\HPDG808_artefacts\Release\VST3\HPDG 808.vst3"
#endif
#ifndef SourceStandalone
#define SourceStandalone "..\build\HPDG808_artefacts\Release\Standalone\HPDG 808.exe"
#endif
#ifndef OutputDir
#define OutputDir "..\Releases"
#endif
#ifndef VCRedistFile
#define VCRedistFile "..\build\prerequisites\vc_redist.x64.exe"
#endif
#define RuntimeRequiredVersion GetFileVersion(VCRedistFile)
[Setup]
#ifdef InstallerTest
AppId=HPDG808-Isolated-Installer-Test
PrivilegesRequired=lowest
CreateUninstallRegKey=no
OutputBaseFilename=HPDG808_InstallTest
#else
AppId={{4F1B7E2A-9C35-4D7B-8A61-2E0C9D5B8F43}
PrivilegesRequired=admin
OutputBaseFilename=HPDG808_Setup_{#MyVersionTag}
#endif
AppName=HPDG 808
AppVersion={#MyAppVersion}
AppVerName=HPDG 808 {#MyAppVersion}
AppPublisher=HamloProd
VersionInfoVersion={#MyFileVersion}
VersionInfoCompany=HamloProd
VersionInfoCopyright=Copyright (C) 2026 HamloProd
DefaultDirName={commonpf64}\HamloProd\HPDG 808
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
#ifdef InstallerTest
Compression=lzma2/fast
#else
Compression=lzma2/ultra64
#endif
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no
UninstallDisplayIcon={app}\HPDG 808.exe
InfoBeforeFile=..\docs\hpdg808\RELEASE.txt
InfoAfterFile=..\docs\hpdg808\INSTALL-FINISH.txt
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Types]
Name: "full"; Description: "VST3 and Standalone"
Name: "custom"; Description: "Choose"; Flags: iscustom
[Components]
Name: "vst3"; Description: "HPDG 808 VST3 (Common Files\VST3)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "HPDG 808 Standalone (no DAW needed)"; Types: full custom
[Files]
#ifdef InstallerTest
Source: "{#SourceVst3Dir}\*"; DestDir: "{app}\VST3\HPDG 808.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#else
Source: "{#SourceVst3Dir}\*"; DestDir: "{commoncf64}\VST3\HPDG 808.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "{#SourceStandalone}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\docs\hpdg808\GUIDE_EN.html"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\docs\hpdg808\RELEASE.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\RIGHTS.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\JUCE\LICENSE.md"; DestDir: "{app}\Documentation\ThirdParty\JUCE"; Flags: ignoreversion
Source: "{#VCRedistFile}"; Flags: dontcopy
#ifndef InstallerTest
[Icons]
Name: "{commonprograms}\HamloProd\HPDG 808"; Filename: "{app}\HPDG 808.exe"; Components: standalone
Name: "{commonprograms}\HamloProd\HPDG 808 Guide"; Filename: "{app}\Documentation\GUIDE_EN.html"
Name: "{commonprograms}\HamloProd\Uninstall HPDG 808"; Filename: "{uninstallexe}"
#endif
#ifndef InstallerTest
[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\HPDG 808.vst3"
#endif
[Run]
Filename: "{app}\Documentation\GUIDE_EN.html"; Description: "Open the HPDG 808 guide"; Flags: postinstall shellexec skipifsilent unchecked
Filename: "{app}\HPDG 808.exe"; Description: "Start HPDG 808 Standalone"; Components: standalone; Flags: postinstall nowait skipifsilent unchecked
[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
  RuntimeVersion: String;
  Installed: Cardinal;
  InstalledVersion, RequiredVersion: Int64;
begin
  Result := '';
#ifdef InstallerTest
  { Payload test runs on the development machine without changing its runtime. }
  Exit;
#endif
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) then
    if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', RuntimeVersion) then
      if StrToVersion(Copy(RuntimeVersion, 2, Length(RuntimeVersion)), InstalledVersion) and StrToVersion('{#RuntimeRequiredVersion}', RequiredVersion) then
        if ComparePackedVersion(InstalledVersion, RequiredVersion) >= 0 then Exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
  begin
    Result := 'Could not start Microsoft Visual C++ Runtime setup.';
    Exit;
  end;
  if ExitCode = 3010 then NeedsRestart := True
  else if (ExitCode <> 0) and (ExitCode <> 1638) then
    Result := Format('Microsoft Visual C++ Runtime setup failed (code %d).', [ExitCode]);
end;
