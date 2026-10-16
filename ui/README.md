# OpenVPN 桌面界面

这是 OpenVPN 控制台的 Flutter 桌面界面，文案和诊断信息均为中文。

当前开发工具链：Flutter 3.47.5，Dart 3.13.4。SDK 位于工作区 `.tools/flutter`，macOS
构建平台目录和 Windows 构建平台目录已纳入项目。

本地验证命令：

```bash
FLUTTER_SUPPRESS_ANALYTICS=true \
XDG_CONFIG_HOME=/绝对路径/OpenVPN/.tools/flutter-config \
PUB_CACHE=/绝对路径/OpenVPN/.tools/pub-cache \
/绝对路径/OpenVPN/.tools/flutter/bin/flutter test
```

当前界面包含：

- 默认深色高级控制台：环境光背景、状态指标卡片、分层会话卡片和高对比连接状态
- 左侧配置/连接导航
- 活动 OpenVPN 会话卡片
- 每个会话独立的连接、断开和状态显示
- 服务器、接口、时长、流量和分流路由摘要
- 路由冲突的行内提示
- 系统浅色/深色主题
- 每个连接独立的路由冲突策略：兼容、严格、询问，以及 IP/CIDR/域名后缀规则
- 连接卡片支持拖动排序，排序会更新连接优先级
- 通过系统文件选择器导入 `.ovpn`/`.conf`，连接时临时输入用户名和密码
- daemon 事件与响应共用一条长连接，能在多会话事件交错时保持会话隔离

真实引擎通过 `EngineClient` 接入；macOS 开发版提供 `UnixSocketEngineClient`，连接
`openvpn_daemon` 的用户权限 Unix socket。Windows 将复用相同命令协议，底层替换为受保护
的 named pipe；当前 macOS 已接入真实 daemon，Windows UI 会明确提示服务端尚未接入，
不会把演示状态伪装成已连接。

界面测试仍可注入内存客户端，避免测试依赖真实 OpenVPN。真实凭据只应由本机交互输入或
Keychain/Credential Manager 提供，不能写入 UI 日志。导入的 profile 只保存在当前进程内，
重新启动应用后需要重新导入；这是避免把内嵌证书/私钥写入普通配置文件的安全取舍。

macOS 联调顺序：先启动 `openvpn_daemon --socket /tmp/openvpn-ui.sock`，再启动 Flutter 应用，
然后导入 profile、输入凭据并连接。严格/询问模式发生路由冲突时，卡片会显示 daemon
返回的诊断，用户可以先断开提示中的前一个会话，再点击连接重试；兼容模式会显示被跳过
的冲突路由并保留前一个会话的路由所有权。
