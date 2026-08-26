#ifndef PluginName
  #error PluginName must be supplied by Package-Windows.ps1
#endif
#ifndef PluginDisplayName
  #error PluginDisplayName must be supplied by Package-Windows.ps1
#endif
#ifndef PluginVersion
  #error PluginVersion must be supplied by Package-Windows.ps1
#endif
#ifndef PluginPublisher
  #error PluginPublisher must be supplied by Package-Windows.ps1
#endif
#ifndef PluginWebsite
  #error PluginWebsite must be supplied by Package-Windows.ps1
#endif
#ifndef StageDir
  #error StageDir must be supplied by Package-Windows.ps1
#endif
#ifndef SourceRoot
  #error SourceRoot must be supplied by Package-Windows.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by Package-Windows.ps1
#endif
#ifndef OutputName
  #error OutputName must be supplied by Package-Windows.ps1
#endif

[Setup]
AppId={{8AEFDC20-729B-42FD-9B4A-D1FAD2819855}
AppName={#PluginDisplayName}
AppVersion={#PluginVersion}
AppVerName={#PluginDisplayName} {#PluginVersion}
AppPublisher={#PluginPublisher}
AppPublisherURL={#PluginWebsite}
AppSupportURL={#PluginWebsite}/issues
AppUpdatesURL={#PluginWebsite}/releases
AppCopyright=Copyright (C) 2026 {#PluginPublisher}
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DefaultGroupName={#PluginDisplayName}
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
CloseApplications=yes
CloseApplicationsFilter=*.dll
RestartApplications=no
UsePreviousAppDir=yes
DirExistsWarning=no
AllowUNCPath=no
SetupLogging=yes
UninstallDisplayName={#PluginDisplayName}
UninstallDisplayIcon={app}\bin\64bit\{#PluginName}.dll
VersionInfoCompany={#PluginPublisher}
VersionInfoDescription={#PluginDisplayName} installer
VersionInfoProductName={#PluginDisplayName}
VersionInfoProductVersion={#PluginVersion}
VersionInfoTextVersion={#PluginVersion}

[Files]
Source: "{#StageDir}\{#PluginName}\bin\64bit\{#PluginName}.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#StageDir}\{#PluginName}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\bin\64bit\{#PluginName}.pdb"
Type: files; Name: "{app}\bin\64bit\{#PluginName}.lib"
Type: files; Name: "{app}\bin\64bit\{#PluginName}.exp"

[UninstallDelete]
Type: dirifempty; Name: "{app}\bin\64bit"
Type: dirifempty; Name: "{app}\bin"
Type: dirifempty; Name: "{app}\data\locale"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}"
