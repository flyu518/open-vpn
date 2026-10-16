# Windows 平台适配器

Windows helper 入口已经接入 OpenVPN3 官方 Windows agent 的 Service、命名管道和
Wintun/TAP 构建边界，并使用 `openvpn` agent 名称与 OpenVPN Connect 隔离。

`core/tools/windows_daemon_main.cpp` 现在提供用户权限的 OpenVPN daemon，使用
`\\.\pipe\openvpn-ui` 接受与 macOS Unix socket 相同的一行 JSON 命令，凭据使用
Windows Credential Manager 保存。

真实 Windows 主机仍需完成 Wintun 驱动安装、Service 注册、路由/DNS 实机验证以及
Windows agent 的多会话运行验证；macOS 交叉编译只能验证源码和目标链接，不能替代这些步骤。
