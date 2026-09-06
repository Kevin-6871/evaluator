; 由 Inno Setup 脚本向导 生成的脚本.汉化:MonKeyDu
; 有关创建INNO安装脚本文件的详细信息，请参阅相关文档！
; 仅限非商业用途.

#define MyAppName "evaluator"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "Kevin-6871"
#define MyAppURL "https://github.com/Kevin-6871/"
#define MyAppExeName "QT.exe"
#define MyAppAssocName "cpp源文件"
#define MyAppAssocExt ".cpp"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt
#define DoubleAmp(Value) StringChange(Value, "&", "&&")
#define EscapeConstArgument(Value) StringChange(StringChange(StringChange(Value, "%", "%25"), ",", "%2c"), "}", "%7d")

[Setup]
; 注意：AppId 的值唯一标识此应用程序。不要在其他应用程序的安装程序中使用相同的 AppId 值.
; （要生成新的 GUID，请在 IDE 内点击 工具 | 生成 GUID）
AppId={{D7788460-DA3C-4973-9993-3B5CE4522113}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={cm:NameAndVersion,{#EscapeConstArgument(MyAppName)},{#EscapeConstArgument(MyAppVersion)}}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
; "ArchitecturesAllowed=x64compatible" 指定 Setup 只能在 x64 和 Arm 架构的 Windows 11 上运行.
ArchitecturesAllowed=x64compatible
; "ArchitecturesInstallIn64BitMode=x64compatible" 请求在 x64 或 Windows 11on Arm 上以“ 64 位模式”安装.
; 这意味着它应该使用本地的 64 位程序文件目录以及注册表的 64 位视图.
ArchitecturesInstallIn64BitMode=x64compatible
; 取消注释以下行以使用 64 位安装程序.
;SetupArchitecture=x64
ChangesAssociations=yes
DisableProgramGroupPage=yes
LicenseFile=C:\Users\shaog\Desktop\mycpp\QT\LICENSE
; 取消注释以下行以在非管理员安装模式下运行（仅为当前用户安装）.
;PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=C:\Users\shaog\Desktop
OutputBaseFilename=rele-setup
SolidCompression=yes
WizardStyle=modern windows11

[Languages]
Name: "chs"; MessagesFile: "compiler:Default.isl"
Name: "english"; MessagesFile: "compiler:Languages\English.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkablealone

[Files]
Source: "C:\Users\shaog\Desktop\mycpp\QT-rele\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 注意：不要在任何共享系统文件上使用“Flags: ignoreversion”.

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#DoubleAmp(MyAppName)}}"; Flags: nowait postinstall skipifsilent

