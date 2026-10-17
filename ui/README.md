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
- macOS 状态栏和 Windows 通知区域托盘常驻，窗口关闭后仍保持 UI 进程；托盘菜单显示服务状态、活动连接、
  持续时间和流量，并支持直接打开、连接、断开、设置、关于和退出
- 桌面设置支持真实的 macOS LaunchAgent / Windows 当前用户 Run 开机自启
- 关于页面支持从 GitLab latest Release 检查版本；用户确认后下载并打开 `.pkg`、`.exe` 或 `.msi` 安装包

真实引擎通过 `EngineClient` 接入；macOS 开发版提供 `UnixSocketEngineClient`，连接
`openvpn_daemon` 的用户权限 Unix socket。Windows 使用 `WindowsNamedPipeEngineClient`，
连接用户 daemon 的 `\\.\pipe\openvpn-ui`；Windows 主机仍需完成 Service/Wintun 的真实
安装和运行验证，不会把演示状态伪装成已连接。

界面测试仍可注入内存客户端，避免测试依赖真实 OpenVPN。应用只持久化 profile 路径、显示名称和
路由策略，不把 profile 内容、内嵌证书/私钥或密码写入普通配置文件；启动时会按原路径重新读取
profile，文件被移动或删除时会提示重新导入。真实凭据只应由本机交互输入或
Keychain/Credential Manager 提供，不能写入 UI 日志。

macOS 联调顺序：先启动 `openvpn_daemon --socket /tmp/openvpn-ui.sock`，再启动 Flutter 应用，
然后导入 profile、输入凭据并连接。严格/询问模式发生路由冲突时，卡片会显示 daemon
返回的诊断，用户可以先断开提示中的前一个会话，再点击连接重试；兼容模式会显示被跳过
的冲突路由并保留前一个会话的路由所有权。

## 构建和 GitLab 更新配置

当前 UI 版本为 `0.1.1`。如果不传更新参数，关于页面会明确提示“当前构建未配置 GitLab 更新地址”。
发布构建时传入：

```bash
--dart-define=OPENVPN_VERSION=0.1.1 \
--dart-define=OPENVPN_GITLAB_API=https://gitlab.example.com/api/v4 \
--dart-define=OPENVPN_GITLAB_PROJECT=group/open-vpn
```

GitLab tag 与 `OPENVPN_VERSION` 对齐，Release 资产建议使用 `OpenVPN-<版本>-macos.pkg`、
`OpenVPN-<版本>-windows.exe` 或 `OpenVPN-<版本>-windows.msi`。更新器不会把 token 嵌入应用，
也不会在后台静默替换当前程序。
