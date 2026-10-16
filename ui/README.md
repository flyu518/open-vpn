# OpenVPN 桌面界面

这是 OpenVPN 控制台的 Flutter 桌面界面，文案和诊断信息均为中文。

当前开发工具链：Flutter 3.47.5，Dart 3.13.4。SDK 位于工作区 `.tools/flutter`，macOS
构建平台目录和 Windows 构建平台目录已纳入项目。

本地验证命令：

```bash
FLUTTER_SUPPRESS_ANALYTICS=true \
XDG_CONFIG_HOME=/绝对路径/open-vpn/.tools/flutter-config \
PUB_CACHE=/绝对路径/open-vpn/.tools/pub-cache \
/绝对路径/open-vpn/.tools/flutter/bin/flutter test
```

当前界面包含：

- 跟随系统浅色/深色主题的紧凑控制台，服务状态使用“检查中/已就绪/未连接”三态显示
- 左侧连接总览和配置管理导航；未实现的连接记录不会伪装成可用功能
- 紧凑的活动 OpenVPN 连接列表，连接、断开、重试和断开中的状态互斥
- 每个会话独立的连接、断开和状态显示
- 服务器、接口、时长、流量和分流路由摘要
- 路由冲突的行内提示
- 系统浅色/深色主题
- 每个连接独立的路由冲突策略：兼容、严格、询问，以及 IP/CIDR/域名后缀规则
- 连接卡片支持拖动排序，排序会更新连接优先级
- 通过系统文件选择器导入 `.ovpn`/`.conf`，自动读取 Access Server 用户名和名称
- 支持首次保存到系统安全存储，后续连接可直接复用已保存凭据
- daemon 事件与响应共用一条长连接，能在多会话事件交错时保持会话隔离

真实引擎通过 `EngineClient` 接入；macOS 开发版提供 `UnixSocketEngineClient`，连接
`openvpn_daemon` 的用户权限 Unix socket。Windows 将复用相同命令协议，底层替换为受保护
的 named pipe；当前 macOS 已接入真实 daemon，Windows UI 会明确提示服务端尚未接入，
不会把演示状态伪装成已连接。

界面测试仍可注入内存客户端，避免测试依赖真实 OpenVPN。应用只持久化 profile 路径、显示名称和
路由策略，不把 profile 内容、内嵌证书/私钥或密码写入普通配置文件；启动时会按原路径重新读取
profile，文件被移动或删除时会提示重新导入。真实凭据只应由本机交互输入或
Keychain/Credential Manager 提供，不能写入 UI 日志。

macOS 联调顺序：先启动 `openvpn_daemon --socket /tmp/openvpn-ui.sock`，再启动 Flutter 应用，
然后导入 profile、输入凭据并连接。严格/询问模式发生路由冲突时，卡片会显示 daemon
返回的诊断，用户可以先断开提示中的前一个会话，再点击连接重试；兼容模式会显示被跳过
的冲突路由并保留前一个会话的路由所有权。
