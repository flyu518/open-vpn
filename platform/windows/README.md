# Windows 平台适配器

Windows helper 入口已经接入 OpenVPN3 官方 Windows agent 的 Service、命名管道和
Wintun/TAP 构建边界，并使用 `openvpn` agent 名称与 OpenVPN Connect 隔离。

`core/tools/windows_daemon_main.cpp` 现在提供用户权限的 OpenVPN daemon，使用
`\\.\pipe\openvpn-ui` 接受与 macOS Unix socket 相同的一行 JSON 命令，凭据使用
Windows Credential Manager 保存。

Windows daemon 的会话上限为 `0`，每条配置会话独立管理自己的 TUN、进程句柄、事件、路由和
DNS 状态；生成的 Windows agent 还会把跨命名管道的 `tun-open`/`tun-setup` 请求重新关联到同一
个 DCO 网卡。这样 Windows 与 macOS 使用相同的多连接逻辑和 UI 交互。

对 `third_party/openvpn3.lock` 固定的官方 OpenVPN3 Windows 源码分析确认，官方 `MyListener`
原本只有一套 TUN、客户端进程句柄和销毁事件，并在 `/tun-open` 前销毁上一条连接；这正是原始
实现只能单连接的原因。本项目保留官方客户端的 JSON/命名管道协议，只在构建时生成按会话隔离
资源的 agent 源码，因此不需要修改 macOS 交互或 Windows UI。

真实 Windows 主机仍需完成 Wintun 驱动安装、Service 注册、路由/DNS 实机验证以及双配置连接验收；
使用 `platform/windows/tests/windows_multisession_smoke.ps1` 执行完整的 A/B 同时连接、独立断开和
资源清理检查。
