# Windows VPN Service 协议

Windows 桌面应用通过受保护的本地 IPC 请求 helper 执行 Wintun/TAP、路由和 DNS 操作。

协议包含以下基本能力：

- 会话标识和调用方校验
- 建立/销毁虚拟网卡
- 添加/删除分流路由
- 添加/删除远端 VPN 服务器旁路路由
- 幂等清理和崩溃恢复

高权限 `main.cpp` 负责 OpenVPN3 官方 Windows agent 的 Service、Wintun/TAP 和 agent
named pipe；用户权限 JSON daemon 位于 `core/tools/windows_daemon_main.cpp`，使用
`\\.\pipe\openvpn-ui` 给 Qt 桌面应用提供与 macOS 相同的 connect、disconnect、snapshot、
凭据和事件协议。两条 named pipe 不复用。

Windows 已有本机标准 EXE 安装、Service 启动、普通用户 IPC 和真实双连接验证记录，
本地核心用例另验证了路由/DNS 隔离及清理，详见 [Windows 验收记录](../../../docs/windows-acceptance.md)。
这些结果不代表所有 Windows 版本、安全策略和异常路径均已覆盖；新候选必须在目标主机回归，
macOS 上的交叉编译或语法检查不能替代 Windows 实机验证。
