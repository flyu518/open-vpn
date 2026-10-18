# Windows 打包

发布包需要包含桌面应用、OpenVPN3 Windows Service、用户级 `openvpn_windows_daemon` 及其隐藏启动器、
Wintun/TAP 依赖，并完成代码签名。

Windows UI daemon 使用 `\\.\pipe\openvpn-ui`；官方高权限 agent 使用自己的
`openvpn` named pipe。两条管道职责分离，避免把桌面 JSON 协议直接暴露给高权限 agent。

当前 CMake 已把固定版本的官方 Windows agent 接入 `openvpn_windows_service`，并新增
`openvpn_windows_daemon` 构建目标。每条 UI 配置会话拥有独立的 TUN、事件、句柄、路由和 DNS
生命周期；Windows 运行验证必须在 Windows 主机完成，仍需确认 Service 注册、Wintun 驱动和真实
双配置连接。

## 托盘、自启和 GitHub 发布

Windows 应用启动后会在通知区域显示 OpenVPN 图标。关闭窗口只隐藏主窗口，托盘菜单可以打开主界面、
查看每条连接的状态/流量、连接或断开配置、打开设置、关于和退出；版本检查统一放在“关于 OpenVPN”中。

“开机自启”使用当前用户的：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run\OpenVPN
```

使用 `-BuildInstaller` 会通过 Inno Setup 6 生成标准中文安装向导和单文件 `.exe` 安装器，
其中直接嵌入应用文件，不会先打开命令行或让用户解压压缩包。安装器以管理员权限安装，
注册并启动 Windows Service，并提供桌面快捷方式选项。完成页以原用户身份启动桌面应用；
桌面应用在发送首个服务请求前，以当前用户权限启动并确认用户级 daemon 已就绪。开机自启
仍由应用“设置”中的当前用户开关管理，因此不会由提升权限的安装器写入用户注册表：

```powershell
.\packaging\windows\build-qt-package.ps1 -BuildInstaller `
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

界面的自启开关不要求管理员权限；安装服务的安装包需要管理员授权。Qt 桌面界面在 Windows 主机使用 MSVC kit 构建：

```powershell
cmake -S desktop/qt -B build/qt-windows -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DCMAKE_BUILD_TYPE=Release
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

正式发布前仍需在具有管理员权限的 Windows 主机上完成安装器安装、Windows Service、Wintun、
签名和真实双配置连接验收；当前构建脚本不会把离线 UI 测试替代为真实连接成功。
正式发布时把安装程序上传到 GitHub Release，资产名称建议为 `OpenVPN-0.0.3-windows-x64.exe` 或
`OpenVPN-0.0.3-windows-x64.msi`。关于入口位于状态栏/托盘菜单，那里只检查 GitHub Releases；用户确认后才启动安装程序，不会静默替换当前应用。

Windows agent 现在按会话隔离多条 TUN；daemon 不再以单连接上限拒绝第二条连接。多隧道、驱动、安装升级和通知区域仍需 Windows 实机验收；不能以 macOS 测试或离线路由模型测试替代真实 Windows 双配置验收。

Windows 的具体验收步骤见[Windows 桌面版验收说明](../docs/windows-acceptance.md)。
