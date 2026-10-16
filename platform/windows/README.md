# Windows 平台适配器

Windows helper 入口已经接入 OpenVPN3 官方 Windows agent 的 Service、命名管道和
Wintun/TAP 构建边界，并使用 `vpn` agent 名称与 OpenVPN Connect 隔离。

真实 Windows 主机仍需完成 Wintun 驱动安装、Service 注册、路由/DNS 实机验证以及
Windows agent 的多会话运行验证；macOS 交叉编译不能替代这些步骤。
