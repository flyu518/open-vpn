# VPN 桌面界面

这是 VPN 控制台的 Flutter 桌面界面，文案和诊断信息均为中文。

当前界面包含：

- 左侧配置/连接导航
- 活动 VPN 会话卡片
- 每个会话独立的连接、断开和状态显示
- 服务器、接口、时长、流量和分流路由摘要
- 路由冲突的行内提示
- 系统浅色/深色主题
- 每个连接独立的路由冲突策略：兼容、严格、询问，以及 IP/CIDR/域名后缀规则

真实引擎通过 `EngineClient` 接入；macOS 开发版提供 `UnixSocketEngineClient`，连接
`vpn_daemon` 的用户权限 Unix socket。Windows 将复用相同命令协议，底层替换为受保护
的 named pipe。

界面测试仍可注入内存客户端，避免测试依赖真实 VPN。真实凭据只应由本机交互输入或
Keychain/Credential Manager 提供，不能写入 UI 日志。
