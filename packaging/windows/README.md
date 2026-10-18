# Windows 打包

版本、连接超时和认证重试策略统一读取根目录 `project-config.json`。`-Version` 可省略，
显式指定时必须与共享配置一致；不得靠修改参数生成不同平台版本。
共同步骤见根目录[发布操作约定](../../RELEASING.md)。

发布包需要包含桌面应用、OpenVPN3 Windows Service、用户级 `openvpn_windows_daemon` 及其隐藏启动器、
Wintun/TAP 依赖，并完成代码签名。

Windows UI daemon 使用 `\\.\pipe\openvpn-ui`；官方高权限 agent 使用自己的
`openvpn` named pipe。两条管道职责分离，避免把桌面 JSON 协议直接暴露给高权限 agent。

当前 CMake 已把固定版本的官方 Windows agent 接入 `openvpn_windows_service`，并新增
`openvpn_windows_daemon` 构建目标。每条 UI 配置会话拥有独立的 TUN、事件、句柄、路由和 DNS
生命周期。Windows 0.0.14 已在本机完成标准 EXE 升级和真实双配置连接验证，
详细结果及未测项目见 [Windows 验收记录](../../docs/windows-acceptance.md)；每个新候选包仍须重新验收。

## 托盘、自启和 GitHub 发布

Windows 应用启动后会在通知区域显示 OpenVPN 图标。关闭窗口只隐藏主窗口，托盘菜单可以打开主界面、
查看每条连接的状态/流量、连接或断开配置、打开设置、关于和退出；版本检查统一放在“关于 OpenVPN”中。

“开机自启”使用当前用户“开始”菜单的 `Programs\Startup\OpenVPN.lnk` 快捷方式；
应用会校验快捷方式目标，并在启用时迁移、删除旧版注册表启动项：

```text
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\OpenVPN.lnk
```

使用 `-BuildInstaller` 会通过 Inno Setup 6 生成标准中文安装向导和单文件 `.exe` 安装器，
其中直接嵌入应用文件，不会先打开命令行或让用户解压压缩包。安装器以管理员权限安装，
注册并启动 Windows Service，并提供桌面快捷方式选项。完成页以原用户身份启动桌面应用；
桌面应用在发送首个服务请求前，以当前用户权限启动并确认用户级 daemon 已就绪。开机自启
仍由应用“设置”中的当前用户开关管理，因此不会由提升权限的安装器写入用户注册表：

```powershell
.\packaging\windows\build-qt-package.ps1 -BuildInstaller `
  -VcRedist C:\deps\VC_redist.x64.exe `
  -WindowsDaemon build\core-windows\Release\openvpn_windows_daemon.exe `
  -WindowsDaemonLauncher build\core-windows\Release\openvpn_windows_daemon_launcher.exe `
  -WindowsService build\core-windows\Release\openvpn_windows_service.exe `
  -WintunDll third_party\wintun\wintun.dll `
  -RuntimeDllDir build\core-windows\Release
```

打包机器需要安装 Inno Setup 6（ISCC.exe）。安装向导在替换文件前会停止旧版 Windows Service、
daemon 和 `OpenVPN.exe` 桌面进程，再覆盖安装文件；
daemon 由隐藏启动器以 `CREATE_NO_WINDOW` 启动，不会先弹出命令行窗口。Windows Service
单独停止和启动，避免新版 EXE 因单实例锁弹出“已有 OpenVPN 实例正在运行”。手动同时
启动两个桌面 EXE 时仍保留单实例保护。

正式 EXE 必须通过 `-VcRedist` 指定微软签名有效的 x64 VC++ Redistributable（当前工具链要求 14.43 或更新），
可从 [微软官方下载说明](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) 获取。
必须使用完整下载的 EXE，不能使用 `C:\ProgramData\Package Cache` 中可能不含 MSI 载荷的缓存启动器；打包检查会拒绝这类小型文件。
安装器离线携带该依赖，检查已安装版本；版本足够新时跳过，否则在启动 VPN 服务前安装，禁止强制重启。
不从开发机的 System32 随意复制 DLL，也不依赖目标机器碰巧装过其他软件的运行库。升级工具链时须同步核对运行库最低要求。

界面的自启开关不要求管理员权限；安装服务的安装包需要管理员授权。Qt 桌面界面在 Windows 主机使用 MSVC kit 构建：

```powershell
cmake -S desktop/qt -B build/qt-windows -UOPENVPN_APP_VERSION -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt-windows --config Release
C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --qmldir desktop/qt/qml build/qt-windows/Release/openvpn_qt.exe
```

如需开发用的便携压缩包（不是安装程序），可不传 `-BuildInstaller`：

```powershell
.\packaging\windows\build-qt-package.ps1 `
  -QtDir C:\Qt\6.8.3\msvc2022_64 `
  -WindowsDaemon build\core-windows\Release\openvpn_windows_daemon.exe `
  -WindowsDaemonLauncher build\core-windows\Release\openvpn_windows_daemon_launcher.exe `
  -WindowsService build\core-windows\Release\openvpn_windows_service.exe `
  -WintunDll third_party\wintun\wintun.dll `
  -RuntimeDllDir build\core-windows\Release
```

每个正式候选都需在 Windows 主机上安装，并检查 Windows Service、Wintun、签名和真实双配置连接；
安装服务时使用管理员授权，桌面应用以普通用户身份验收。已有 0.0.14 实测记录不替代新包的回归，
离线 UI 测试也不代表真实连接成功。
正式发布时按共同验收关卡，把安装程序上传到包含全部目标平台安装包的同一个 GitHub Release。
先打包、实际安装并验收，再创建统一版本标签；上传的必须是已验收且哈希一致的 EXE。当前 Mac 工作流不会等待 Windows，跨平台发布关卡补齐前不要推正式标签。
资产名称为 `OpenVPN-<版本>-windows-x64.exe`；当前脚本生成 EXE，不把 MSI 示例当作已有产物。
关于入口位于状态栏/托盘菜单，那里只检查 GitHub Releases；用户确认后才启动安装程序，不会静默替换当前应用。

Windows agent 按会话隔离多条 TUN；daemon 不以单连接上限拒绝第二条连接。多隧道和安装升级已有本机实测记录；托盘的工具操作限制与用户人工反馈分别记录。不能以 macOS 测试或离线路由模型测试替代 Windows 安装包实机验收。

Windows 的具体验收步骤见[Windows 桌面版验收说明](../../docs/windows-acceptance.md)。

安装器中的 `install-iexpress.cmd` 删除项和打包脚本中的排除项用于清理旧安装残留，不是旧安装方案仍在运行；不要移除这两处升级保护。开发 ZIP 和本机历史 EXE 不应混入正式 Release。
