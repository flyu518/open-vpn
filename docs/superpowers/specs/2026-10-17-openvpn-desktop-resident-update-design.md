# OpenVPN 桌面常驻与更新设计

## 目标

为 macOS 和 Windows 桌面版 OpenVPN 增加轻量的系统状态栏/托盘常驻能力，使用户不打开主窗口也能看到服务和连接状态、快速打开应用或连接/断开某条配置，并提供真实可用的开机自启、关于和 GitLab 版本检查。

## 用户场景

- 用户平时把 OpenVPN 放在状态栏/托盘中运行，主窗口关闭后连接仍然保持。
- 用户点击图标后能看到当前活动连接数、每条连接的名称、服务器、连接状态、持续时间和流量摘要。
- 用户可以从托盘直接打开主界面，或对已有配置执行连接、断开。
- 用户在“关于 OpenVPN”中看到当前版本并手动检查更新；发现新版本后，用户确认再下载并打开对应的系统安装包。
- 用户可以在主界面或托盘菜单中开启/关闭登录后自动启动。

## 目标

1. macOS 状态栏和 Windows 通知区域提供相同的核心功能。
2. 窗口关闭默认隐藏到托盘，不终止 VPN 会话；菜单中的“退出 OpenVPN”才真正退出 UI。
3. 托盘信息来自现有 `SessionViewModel`，不复制一套连接状态。
4. 开机自启使用各平台原生机制：macOS 用户级 LaunchAgent，Windows 当前用户 Run 注册表项。
5. GitLab 更新检查支持公开项目的 latest release API、版本比较、平台安装包选择和用户确认下载。
6. 网络失败、未配置 GitLab 地址、缺少平台安装包时给出明确中文提示，不影响正常 VPN 使用。
7. 现有 Android/iOS 扩展边界不被破坏；移动端继续不实现托盘功能。

## 非目标

- 不在本阶段实现 Android/iOS 状态栏能力。
- 不在后台静默升级、替换正在运行的程序或自动重启 UI。
- 不把 GitLab 私有 token 写进应用包；私有仓库认证作为后续能力。
- 不把 helper、daemon 或驱动的安装/升级偷偷绑定到 UI 更新按钮；安装包由用户确认执行。
- 不重新实现 OpenVPN 连接核心；托盘只是现有 UI 的控制入口。

## 交互设计

### 窗口和托盘

- 启动后创建一个 OpenVPN 图标，macOS 显示在菜单栏，Windows 显示在通知区域。
- 点击托盘图标或“打开 OpenVPN”显示并聚焦主窗口。
- 点击窗口关闭按钮只隐藏窗口；“退出 OpenVPN”销毁托盘、停止 UI 进程并释放 UI 资源，活动 VPN 是否断开由现有 daemon/helper 生命周期决定。
- 托盘菜单按以下顺序展示：
  1. `打开 OpenVPN`
  2. 当前服务状态和活动连接数（只读）
  3. 每条配置的连接/断开动作及简短状态
  4. `开机自启`复选项
  5. `检查更新`
  6. `关于 OpenVPN`
  7. `退出 OpenVPN`

### 开机自启

- macOS：写入 `~/Library/LaunchAgents/com.openvpn.ui.plist`，使用当前安装应用的可执行文件路径；启用时 bootstrap，禁用时 bootout 并删除文件。
- Windows：写入 `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\OpenVPN`，值为当前可执行文件路径；禁用时删除该值。
- 读取状态时以系统实际状态为准；权限或系统命令失败时返回中文错误，不把 UI 开关伪装成成功。

### GitLab 更新

- 构建时通过 `--dart-define` 注入：
  - `OPENVPN_GITLAB_API`：例如 `https://gitlab.example.com/api/v4`
  - `OPENVPN_GITLAB_PROJECT`：例如 `group/open-vpn`
- 默认值为空。未配置时“检查更新”明确显示“当前构建未配置 GitLab 更新地址”，而不是访问猜测地址。
- 请求 `/projects/{urlEncode(project)}/releases/permalink/latest`，读取 `tag_name`、`name`、`description` 和 assets links。
- 只接受版本格式 `v?主版本.次版本.修订号`；当前版本大于等于 latest 时显示“已是最新版本”。
- macOS 优先匹配 `.pkg`，Windows 优先匹配 `.exe` 或 `.msi`；没有对应资产时提供 Release 页面链接。
- 用户在更新对话框明确点击“下载并安装”后，将文件保存到临时目录并调用 macOS `open` 或 Windows `Process.start` 启动安装器；下载、启动失败都显示可读错误。

## 技术方案

- Flutter 继续作为桌面 UI；新增 `tray_manager` 负责原生托盘/状态栏，`window_manager` 负责窗口初始化、隐藏和关闭拦截。
- `TrayController` 只做菜单投影和事件转发，不直接保存连接状态；它监听 `SessionViewModel` 刷新菜单。
- `StartupService` 以平台实现封装系统自启，接口保持 `isEnabled/enable/disable`，便于后续测试替换。
- `UpdateService` 以 `HttpClient` 解析 GitLab JSON，并把解析/版本选择拆成无副作用函数，单元测试不需要网络。
- `AboutDialog` 只显示版本、更新入口和安装说明，不把 token、profile 或密码展示到日志。

## 安全和失败处理

- 更新 API 请求只读取公开 Release 元数据；不在日志记录凭据或完整下载 URL 的 query token。
- 下载资产失败不改变当前版本，不删除当前安装。
- 所有系统命令都使用参数数组，不拼接用户输入到 shell 命令。
- 托盘初始化失败时主界面仍然可以使用，并在诊断区域给出一次性提示。
- 当前应用版本从 `pubspec.yaml` 的 `version` 维护，发布时必须与 GitLab tag 对齐。

## 验收标准

- Flutter 测试覆盖版本比较、GitLab release 解析、平台资产选择和自启命令构造。
- macOS 构建产物能启动、窗口关闭后进程仍在托盘中、托盘菜单可打开主窗口。
- Windows 构建通过静态分析/编译；Windows 主机上验证托盘和 Run 注册表项。
- 无 GitLab 配置、网络错误、无更新、存在更新、无平台资产五种更新路径均有中文反馈。
- 现有 Flutter、C++ 测试保持通过。
