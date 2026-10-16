# Windows 打包

发布包需要包含桌面应用、OpenVPN3 Windows Service、用户级 `openvpn_windows_daemon`、
Wintun/TAP 依赖，并完成代码签名。

Windows UI daemon 使用 `\\.\pipe\openvpn-ui`；官方高权限 agent 使用自己的
`openvpn` named pipe。两条管道职责分离，避免把桌面 JSON 协议直接暴露给高权限 agent。

当前 CMake 已把官方 Windows agent 接入 `openvpn_windows_service`，并新增
`openvpn_windows_daemon` 构建目标；Windows 运行验证必须在 Windows 主机完成，仍需确认
Service 注册、Wintun 驱动、路由/DNS 隔离和多会话真实连接。
