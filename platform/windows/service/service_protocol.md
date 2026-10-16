# Windows VPN Service 协议

Windows 桌面应用通过受保护的本地 IPC 请求 helper 执行 Wintun/TAP、路由和 DNS 操作。

第一版协议必须包含：

- 会话标识和调用方校验
- 建立/销毁虚拟网卡
- 添加/删除分流路由
- 添加/删除远端 VPN 服务器旁路路由
- 幂等清理和崩溃恢复

高权限 `main.cpp` 负责 OpenVPN3 官方 Windows agent 的 Service、Wintun/TAP 和 agent
named pipe；用户权限 JSON daemon 位于 `core/tools/windows_daemon_main.cpp`，使用
`\\.\pipe\openvpn-ui` 给 Flutter 提供与 macOS 相同的 connect、disconnect、snapshot、
凭据和事件协议。两条 named pipe 不复用。

当前仍需要 Windows 主机完成 Service 注册、Wintun 驱动、调用方权限和真实多会话路由/DNS
验证；macOS 只能做 MinGW 语法和链接边界检查。
