; SmartScheduler 安装包脚本（Inno Setup 6，中文界面）
; 由 scripts\package.bat 调用 iscc 编译，也可手动：iscc installer\SmartScheduler.iss

#define MyAppName "智排课系统"
#define MyAppNameEn "SmartScheduler"
; 版本号可被命令行覆盖：iscc /DMyAppVersion=1.0.2 ...
#ifndef MyAppVersion
  #define MyAppVersion "1.0.42"
#endif
#define MyAppPublisher "SmartScheduler"
#define MyAppExeName "SmartScheduler.exe"
; 应用标识（AUMID）：应用内弹 Windows 通知时用它归属到本应用，必须与
; src/app/AppController.cpp 里的 kToastAumid 保持一致
#define MyAppAumid "SmartScheduler"

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
; AppUserModelID：让「更新已就绪」等系统通知归属到本应用（未注册的 AUMID 通知可能不显示）
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppAumid}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; AppUserModelID: "{#MyAppAumid}"

[Run]
; postinstall 在静默模式下也会执行（除非 skipifsilent）——一键更新走 /VERYSILENT，
; 曾带 skipifsilent 导致静默更新后不会自动启动新版本；runasoriginaluser 避免以管理员身份启动
Filename: "{app}\{#MyAppExeName}"; Description: "立即运行 {#MyAppName}"; Flags: nowait postinstall runasoriginaluser
