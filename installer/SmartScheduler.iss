; SmartScheduler 安装包脚本（Inno Setup 6，中文界面）
; 由 scripts\package.bat 调用 iscc 编译，也可手动：iscc installer\SmartScheduler.iss

#define MyAppName "智排课系统"
#define MyAppNameEn "SmartScheduler"
; 版本号可被命令行覆盖：iscc /DMyAppVersion=1.0.2 ...
#ifndef MyAppVersion
  #define MyAppVersion "1.0.16"
#endif
#define MyAppPublisher "SmartScheduler"
#define MyAppExeName "SmartScheduler.exe"

[Setup]
AppId={{8F3A2C1E-5B7D-4E9F-A1C2-3D4E5F6A7B8C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\SmartScheduler
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=Output
OutputBaseFilename=SmartScheduler-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\icon.ico
UninstallDisplayIcon={app}\SmartScheduler.exe
ShowLanguageDialog=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} 安装程序
UninstallDisplayName={#MyAppName}

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
Source: "..\deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "立即运行 {#MyAppName}"; Flags: nowait postinstall skipifsilent
