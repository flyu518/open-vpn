# OpenVPN

OpenVPN 是一个面向企业网络的桌面 VPN 管理客户端，用于导入、保存和管理多个 OpenVPN 配置，重点解决多条企业 VPN 需要同时保持连接、分别查看状态以及避免路由互相覆盖的问题。

项目参考官方 OpenVPN Connect 的连接管理体验，并针对多配置同时管理进行了独立实现；本项目不是 OpenVPN 官方客户端，也不隶属于 OpenVPN Inc.。

## 项目特点

- 同时管理多条 OpenVPN 连接，每条连接拥有独立的状态、计时、流量和生命周期；
- 导入 `.ovpn` 或 `.conf` 配置，并保存应用内部副本，原配置文件移动或删除后仍可使用；
- 自动读取配置中的用户名，密码可保存到 macOS 钥匙串或 Windows Credential Manager，认证失败时重新输入；
- 连接列表支持重命名、拖动排序、导入重复检查、配置导出和连接详情查看；
- 详情中显示服务器信息、连接记录、路由冲突、实时流量和历史流量；
- macOS 状态栏和 Windows 通知区域菜单支持快速打开窗口、连接或断开配置、查看状态、设置、关于和退出；
- 支持登录后自动启动，关闭主窗口不会自动断开后台 VPN；
- 关于页面支持检查 GitHub Releases 更新，下载前校验发布资产。

## 支持平台

### macOS

macOS 提供多连接能力，并分别构建两个安装包：

- `x86_64`：Intel Mac；
- `arm64`：Apple Silicon Mac。

Apple Silicon 使用 `arm64` 安装包时不需要 Rosetta。当前构建最低支持 macOS 26，开发包使用临时签名，正式发布还需要 Developer ID 签名和公证。

### Windows

Windows 版本使用 Qt 桌面界面、OpenVPN3 Windows Service 和 Wintun/TAP 适配层，支持多条配置同时连接，不是单连接预览版。Windows daemon 与 macOS 使用相同的会话逻辑，Windows agent 为每条会话隔离 TUN、句柄、路由和 DNS。

Windows `1.0.0` 内部候选已完成本机标准 EXE 覆盖安装和两条真实配置的同时连接、独立断开及重连验证，安装包哈希、源码提交和未测项见 [1.0.0 Windows 候选记录](docs/releases/1.0.0-windows.md)。尚未正式发布；重启后验证、Mac 同候选验收和跨平台发布关卡仍须完成。历史结果保留在 [Windows 验收说明](docs/windows-acceptance.md)。

### Android/iOS

移动端暂未提供客户端。后续将分别使用 Android `VpnService` 和 iOS `NEPacketTunnelProvider`，并保持单条活动系统 VPN 的能力边界。

## 路由处理

默认使用兼容模式：发生重叠时保留先建立连接的路由，后建立连接跳过冲突路由；严格模式会拒绝发生冲突的新连接。

连接详情支持配置 IP/CIDR 规则：

- 优先当前连接：发现重叠时提示先断开冲突连接，再重新连接当前连接；
- 排除当前连接：当前连接不安装匹配的服务器路由；
- 策略修改不会热更新已经建立的隧道，需要断开并重新连接；
- 域名后缀规则会参与两端的系统分流 DNS；任意模糊匹配和正则规则不支持。

## 使用方式

macOS 安装对应架构的 `.pkg` 后打开 `/Applications/OpenVPN.app`；Windows 运行标准 `.exe` 安装向导后，从开始菜单或快捷方式打开 OpenVPN。Windows 安装服务时需要 UAC 授权，日常桌面应用以普通用户身份运行，不需要“以管理员身份运行”。两端操作相同：

1. 点击“导入配置”，选择 `.ovpn` 或 `.conf` 文件；
2. 在连接列表中确认名称和服务器信息；
3. 点击对应卡片的连接按钮；
4. 首次连接输入用户名和密码，并按需选择保存密码；
5. 后续连接会优先使用系统安全存储中的凭据，只有认证失败或凭据不可用时才会再次询问。

## 技术架构

- `desktop/qt/`：Qt 6/QML 桌面界面、状态栏菜单、配置管理和更新检查；
- `core/`：C++ 会话模型、OpenVPN3 引擎、daemon、路由策略和安全处理；
- `platform/macos/`：macOS utun、路由、DNS、Keychain 和高权限 helper；
- `platform/windows/`：Windows Service、Wintun/TAP 和本地安全存储适配；
- `packaging/`：macOS、Windows 构建和安装脚本；
- `docs/`：协议、平台说明和验收记录。

桌面界面通过用户权限 daemon 与 VPN 引擎通信；需要修改系统网络状态的操作由平台 helper 执行。OpenVPN3 源码版本由 `third_party/openvpn3.lock` 固定，构建时检出对应版本到 `third_party/openvpn3/`。

## 开发和构建

开发环境需要 Qt 6.8 或更新版本、CMake 3.21 或更新版本、OpenSSL、asio、fmt、jsoncpp、lz4 和 xxHash。基础测试不需要真实 VPN 配置：

```bash
cmake -S core -B /tmp/openvpn-core-build
cmake --build /tmp/openvpn-core-build --parallel
ctest --test-dir /tmp/openvpn-core-build --output-on-failure
```

生成 macOS 安装包时，使用 `packaging/macos/build-release.sh`。Intel 构建的最后两个参数为 `x86_64` 和 `/usr/local`；Apple Silicon 构建改为 `arm64` 和 `/opt/homebrew`。脚本会构建 core、Qt 应用、daemon 和 helper，运行测试并检查安装包动态库。

更完整的打包说明见[macOS 打包文档](packaging/macos/README.md)，Windows 说明见[Windows 打包文档](packaging/windows/README.md)。

## GitHub Releases

多平台更新、版本递增、构建、安装包验收与发布顺序统一遵循根目录的[发布操作约定](RELEASING.md)。
顺序为：冻结同一源码候选 → 两端分别构建、安装并验收 → 创建唯一 tag 和草稿 Release → Mac、Windows 任一方都可先上传已验收资产，另一方随后补齐 → 汇总工作流核对两端验收 SHA-256 后正式发布。平台提交先后不限；可按标准资产名重命名文件，但不得重新构建，草稿在齐备前保持未发布。
根目录 [project-config.json](project-config.json) 是版本号、连接超时和认证重试策略的共享来源；
修改后必须重新构建，两端不能分别维护版本号。

仓库地址：<https://github.com/flyu518/open-vpn>

`.github/workflows/release-macos.yml` 在推送 `v<版本号>` tag 后只构建 Intel 和 Apple Silicon 安装包并上传为 Actions 产物，不创建或发布 GitHub Release。统一发布由 `.github/workflows/publish-release.yml` 手动执行；它要求草稿中 Windows x64 与 macOS ARM64 安装包均存在，并匹配两端记录的验收 SHA-256。共享配置中的 `1.0.0` 不代表 Release 已发布。

旧版本验收记录和更新比较测试保留原版本号；旧安装脚本的升级清理保护也继续保留。具体范围见发布约定的“旧版本内容与清理边界”，不要据此误判仍在使用旧产品版本。

## 数据和隐私

配置索引和应用副本保存在：

```text
~/Library/Application Support/OpenVPN/
%APPDATA%\OpenVPN\
```

上面两行分别为 macOS 和 Windows 路径。密码使用 macOS 钥匙串或 Windows Credential Manager 保存。历史流量只记录时间和上下行字节，不记录访问内容、密码或私钥文本。日志和错误信息会经过脱敏处理。

## 已知限制

- macOS 当前最低支持 macOS 26；正式签名和公证尚未配置；
- Windows 已有本机安装版双连接验收记录；更新下载安装、卸载、更多系统环境和业务目标可达性仍需单独验收，不能由现有记录推定通过；
- Android/iOS 尚未实现；
- 域名后缀分流依赖 VPN 提供 DNS 服务器；任意模糊匹配和正则规则尚未支持；
- 路由策略修改需要断开并重新连接后生效。

协议定义见[VPN 协议 v1](docs/protocol-v1.md)，当前桌面验收范围见[桌面客户端验收记录](docs/desktop-acceptance.md)。
