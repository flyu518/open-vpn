# Windows 打包

发布包需要包含桌面应用、OpenVPN3 Windows Service、Wintun/TAP 依赖，并完成代码签名。
当前 CMake 已把官方 Windows agent 接入 `openvpn_windows_service` 构建目标；Windows 运行
验证必须在 Windows 主机完成，并需要继续确认多会话路由和 DNS 隔离。
