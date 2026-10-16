# Windows VPN Service 协议

Windows 桌面应用通过受保护的本地 IPC 请求 helper 执行 Wintun/TAP、路由和 DNS 操作。

第一版协议必须包含：

- 会话标识和调用方校验
- 建立/销毁虚拟网卡
- 添加/删除分流路由
- 添加/删除远端 VPN 服务器旁路路由
- 幂等清理和崩溃恢复

当前 `main.cpp` 只是跨平台构建占位符，不会建立 VPN 或修改系统路由。
