# Windows 打包

发布包需要包含桌面应用、OpenVPN3 Windows Service、用户级 `openvpn_windows_daemon`、
Wintun/TAP 依赖，并完成代码签名。

Windows UI daemon 使用 `\\.\pipe\openvpn-ui`；官方高权限 agent 使用自己的
`openvpn` named pipe。两条管道职责分离，避免把桌面 JSON 协议直接暴露给高权限 agent。

当前 CMake 已把官方 Windows agent 接入 `openvpn_windows_service`，并新增
`openvpn_windows_daemon` 构建目标；Windows 运行验证必须在 Windows 主机完成，仍需确认
Service 注册、Wintun 驱动、路由/DNS 隔离和多会话真实连接。

## 托盘、自启和 GitLab 发布

Windows 应用启动后会在通知区域显示 OpenVPN 图标。关闭窗口只隐藏主窗口，托盘菜单可以打开主界面、
查看每条连接的状态/流量、连接或断开配置、打开设置、检查更新和退出。

“开机自启”使用当前用户的：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run\OpenVPN
```

不会要求管理员权限，也不会修改 Windows Service。发布构建时在 Windows 主机执行：

```powershell
flutter build windows --release `
  --dart-define=OPENVPN_VERSION=0.1.1 `
  --dart-define=OPENVPN_GITLAB_API=https://gitlab.example.com/api/v4 `
  --dart-define=OPENVPN_GITLAB_PROJECT=group/open-vpn
```

把生成的安装程序上传到 GitLab Release，资产名称建议为 `OpenVPN-0.1.1-windows.exe` 或
`OpenVPN-0.1.1-windows.msi`。关于页面检查 latest Release 时会优先选择 `.exe`/`.msi`，由用户确认后
才启动安装程序；不会静默替换当前应用。
