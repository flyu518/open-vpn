#ifndef AppVersion
  #error AppVersion is required; build through build-qt-package.ps1 using project-config.json
#endif
#ifndef VcRedistPath
  #error VcRedistPath is required; include the validated Microsoft VC++ x64 redistributable
#endif
#ifndef StageDir
  #define StageDir "."
#endif
#ifndef OutputDirectory
  #define OutputDirectory "."
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "OpenVPN-windows-x64-setup"
#endif

[Setup]
AppId={{7E71C258-32C6-4B90-A73B-9E1B5CEED2B4}
AppName=OpenVPN
AppVersion={#AppVersion}
AppPublisher=OpenVPN
DefaultDirName={autopf}\OpenVPN
DefaultGroupName=OpenVPN
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\OpenVPN.exe
OutputDir="{#OutputDirectory}"
OutputBaseFilename={#OutputBaseFilename}
SetupLogging=yes
WizardStyle=modern
PrivilegesRequired=admin
UsePreviousLanguage=no
LanguageDetectionMethod=none
MissingMessagesWarning=yes
NotRecognizedMessagesWarning=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
Uninstallable=yes
VersionInfoDescription=OpenVPN Windows Installer
VersionInfoProductName=OpenVPN
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "chinesesimp"; MessagesFile: "OpenVPN.zh-CN.isl"

[Messages]
SetupAppTitle=OpenVPN 安装程序
SetupWindowTitle=安装 - %1
UninstallAppTitle=OpenVPN 卸载程序
WizardSelectDir=选择安装位置
ButtonBack=< 上一步(&B)
ButtonNext=下一步(&N) >
ButtonInstall=安装(&I)
ButtonCancel=取消
ButtonFinish=完成(&F)
WelcomeLabel1=欢迎使用 OpenVPN 安装向导
WelcomeLabel2=此向导将安装 OpenVPN {#AppVersion}。%n%n建议先关闭其他应用程序，再继续安装。
SelectDirDesc=您想将 OpenVPN 安装到哪里？
SelectDirLabel3=OpenVPN 将安装到以下文件夹。
SelectDirBrowseLabel=单击“下一步”继续；如需选择其他文件夹，请单击“浏览”。
DiskSpaceMBLabel=至少需要 [mb] MB 可用磁盘空间。
DiskSpaceGBLabel=至少需要 [gb] GB 可用磁盘空间。
WizardReady=准备安装
ReadyLabel1=安装程序已准备就绪，可以开始安装 OpenVPN。
ReadyLabel2a=单击“安装”继续；如需检查或更改设置，请单击“上一步”。
ReadyLabel2b=单击“安装”开始安装。
WizardInstalling=正在安装
InstallingLabel=请稍候，正在安装 OpenVPN。
FinishedHeadingLabel=OpenVPN 安装完成
FinishedLabelNoIcons=OpenVPN 已成功安装到电脑。
FinishedLabel=OpenVPN 已成功安装。可通过创建的快捷方式启动应用。
ClickFinish=单击“完成”关闭安装向导。

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VcRedistPath}"; DestDir: "{tmp}"; DestName: "OpenVPN-vc_redist.x64.exe"; Flags: deleteafterinstall

[InstallDelete]
Type: files; Name: "{app}\install-iexpress.cmd"

[Icons]
Name: "{autoprograms}\OpenVPN"; Filename: "{app}\OpenVPN.exe"
Name: "{autodesktop}\OpenVPN"; Filename: "{app}\OpenVPN.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\OpenVPN.exe"; Description: "启动 OpenVPN"; Flags: postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop openvpn"; Flags: runhidden waituntilterminated; RunOnceId: "StopOpenVPNService"
Filename: "{sys}\taskkill.exe"; Parameters: "/IM openvpn_windows_daemon_launcher.exe /T /F"; Flags: runhidden waituntilterminated; RunOnceId: "StopOpenVPNLauncher"
Filename: "{sys}\taskkill.exe"; Parameters: "/IM openvpn_windows_daemon.exe /T /F"; Flags: runhidden waituntilterminated; RunOnceId: "StopOpenVPNDaemon"
Filename: "{sys}\taskkill.exe"; Parameters: "/IM OpenVPN.exe /T /F"; Flags: runhidden waituntilterminated; RunOnceId: "StopOpenVPNUi"
Filename: "{app}\openvpn_windows_service.exe"; Parameters: "remove"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "RemoveOpenVPNService"

[Code]
const
  SC_MANAGER_CONNECT = 1;
  SERVICE_QUERY_STATUS = 4;
  SERVICE_STOP = 32;
  SERVICE_CONTROL_STOP = 1;
  SERVICE_STOPPED = 1;
  ERROR_SERVICE_DOES_NOT_EXIST = 1060;

type
  TServiceStatus = record
    dwServiceType: LongWord;
    dwCurrentState: LongWord;
    dwControlsAccepted: LongWord;
    dwWin32ExitCode: LongWord;
    dwServiceSpecificExitCode: LongWord;
    dwCheckPoint: LongWord;
    dwWaitHint: LongWord;
  end;

var
  RuntimeRestartRequired: Boolean;

function OpenSCManagerW(lpMachineName, lpDatabaseName: String;
  dwDesiredAccess: LongWord): LongWord;
  external 'OpenSCManagerW@advapi32.dll stdcall delayload';
function OpenServiceW(hSCManager: LongWord; lpServiceName: String;
  dwDesiredAccess: LongWord): LongWord;
  external 'OpenServiceW@advapi32.dll stdcall delayload';
function ControlService(hService: LongWord; dwControl: LongWord;
  var lpServiceStatus: TServiceStatus): LongBool;
  external 'ControlService@advapi32.dll stdcall delayload';
function QueryServiceStatus(hService: LongWord;
  var lpServiceStatus: TServiceStatus): LongBool;
  external 'QueryServiceStatus@advapi32.dll stdcall delayload';
function CloseServiceHandle(hSCObject: LongWord): LongBool;
  external 'CloseServiceHandle@advapi32.dll stdcall delayload';
function GetLastErrorCode: LongWord;
  external 'GetLastError@kernel32.dll stdcall delayload';
function GetTickCount: LongWord;
  external 'GetTickCount@kernel32.dll stdcall delayload';

function StopOpenVpnService(var ErrorText: String): Boolean;
var
  Manager, Service: LongWord;
  Status: TServiceStatus;
  StartTick: LongWord;
begin
  Result := False;
  Manager := OpenSCManagerW('', '', SC_MANAGER_CONNECT);
  if Manager = 0 then begin
    ErrorText := '无法连接 Windows 服务管理器，请使用管理员权限重试。';
    Exit;
  end;

  Service := OpenServiceW(Manager, 'openvpn', SERVICE_QUERY_STATUS or SERVICE_STOP);
  if Service = 0 then begin
    if GetLastErrorCode = ERROR_SERVICE_DOES_NOT_EXIST then
      Result := True
    else
      ErrorText := '无法打开现有 OpenVPN 服务。请先关闭旧版服务后重试。';
    CloseServiceHandle(Manager);
    Exit;
  end;

  if not QueryServiceStatus(Service, Status) then begin
    ErrorText := '无法读取现有 OpenVPN 服务状态。请先关闭旧版服务后重试。';
    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    Exit;
  end;

  if Status.dwCurrentState <> SERVICE_STOPPED then begin
    ControlService(Service, SERVICE_CONTROL_STOP, Status);
    StartTick := GetTickCount;
    while (Status.dwCurrentState <> SERVICE_STOPPED) and
      (GetTickCount - StartTick < 30000) do begin
      Sleep(250);
      if not QueryServiceStatus(Service, Status) then begin
        ErrorText := '等待 OpenVPN 服务关闭时发生错误。请重启电脑后再安装。';
        CloseServiceHandle(Service);
        CloseServiceHandle(Manager);
        Exit;
      end;
    end;
  end;

  Result := Status.dwCurrentState = SERVICE_STOPPED;
  if not Result then
    ErrorText := '等待 OpenVPN 服务关闭超时。请重启电脑后再安装。';
  CloseServiceHandle(Service);
  CloseServiceHandle(Manager);
end;

procedure StopDesktopProcesses;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'),
    '/IM openvpn_windows_daemon_launcher.exe /T /F', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'),
    '/IM openvpn_windows_daemon.exe /T /F', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'),
    '/IM OpenVPN.exe /T /F', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if not StopOpenVpnService(Result) then
    Exit;
  StopDesktopProcesses;
  Result := '';
end;

procedure InstallOrUpdateService;
var
  ResultCode: Integer;
  ScPath, ServicePath: String;
begin
  ScPath := ExpandConstant('{sys}\sc.exe');
  ServicePath := ExpandConstant('{app}\openvpn_windows_service.exe');
  Exec(ScPath, 'query openvpn', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if ResultCode = 0 then begin
    if (not Exec(ScPath, 'config openvpn binPath= ' + AddQuotes(ServicePath) +
      ' start= auto', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or
      (ResultCode <> 0) then
      RaiseException('更新 OpenVPN Windows 服务路径失败，请重新安装。');
  end else if ResultCode = 1060 then begin
    if (not Exec(ServicePath, 'install', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
      RaiseException('注册 OpenVPN Windows 服务失败，请重新安装。');
  end else
    RaiseException('检查现有 OpenVPN Windows 服务失败，请重新安装。');

  if (not Exec(ScPath, 'start openvpn', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode)) or
    ((ResultCode <> 0) and (ResultCode <> 1056)) then
    RaiseException('启动 OpenVPN Windows 服务失败，请重新启动电脑后重试。');
end;

function VisualCppRuntimeReady: Boolean;
var
  Installed, Major, Minor, Build, Revision: Cardinal;
  RequiredMS, RequiredLS, InstalledMS, InstalledLS: Cardinal;
  Key: String;
begin
  Result := False;
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not GetVersionNumbers(ExpandConstant('{tmp}\OpenVPN-vc_redist.x64.exe'), RequiredMS, RequiredLS) then
    Exit;
  if not RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) or (Installed <> 1) or
    not RegQueryDWordValue(HKLM64, Key, 'Major', Major) or
    not RegQueryDWordValue(HKLM64, Key, 'Minor', Minor) or
    not RegQueryDWordValue(HKLM64, Key, 'Bld', Build) or
    not RegQueryDWordValue(HKLM64, Key, 'Rbld', Revision) then
    Exit;
  InstalledMS := (Major shl 16) or Minor;
  InstalledLS := (Build shl 16) or Revision;
  Result := (InstalledMS > RequiredMS) or
    ((InstalledMS = RequiredMS) and (InstalledLS >= RequiredLS));
end;

procedure EnsureVisualCppRuntime;
var
  ResultCode: Integer;
begin
  if VisualCppRuntimeReady then begin
    Log('VC++ x64 runtime already meets the bundled version; skipping prerequisite installation');
    Exit;
  end;
  Log('Installing validated offline Microsoft Visual C++ x64 runtime');
  if not Exec(ExpandConstant('{tmp}\OpenVPN-vc_redist.x64.exe'),
    '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    RaiseException('无法启动 VC++ 运行库安装程序，请重新安装。');
  Log('VC++ runtime exit code: ' + IntToStr(ResultCode));
  { 1638 means a newer runtime is already installed. Never downgrade it. }
  if (ResultCode <> 0) and (ResultCode <> 3010) and (ResultCode <> 1638) then
    RaiseException('VC++ 运行库安装失败（错误码 ' + IntToStr(ResultCode) + '），请检查安装日志。');
  if ResultCode = 3010 then
    RuntimeRestartRequired := True;
end;

function NeedRestart(): Boolean;
begin
  Result := RuntimeRestartRequired;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    EnsureVisualCppRuntime;
    InstallOrUpdateService;
  end;
end;
