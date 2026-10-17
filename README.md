# OpenVPN

OpenVPN 是一个面向企业网络的桌面 VPN 管理客户端，用于导入、保存和管理多个 OpenVPN 配置，重点解决多条企业 VPN 需要同时保持连接、分别查看状态以及避免路由互相覆盖的问题。

项目参考官方 OpenVPN Connect 的连接管理体验，并针对多配置同时管理进行了独立实现；本项目不是 OpenVPN 官方客户端，也不隶属于 OpenVPN Inc.。

## 项目特点

- 同时管理多条 OpenVPN 连接，每条连接拥有独立的状态、计时、流量和生命周期；
- 导入 `.ovpn` 或 `.conf` 配置，并保存应用内部副本，原配置文件移动或删除后仍可使用；
- 自动读取配置中的用户名，密码可保存到 macOS 钥匙串，认证失败时重新输入；
- 连接列表支持重命名、拖动排序、导入重复检查、配置导出和连接详情查看；
- 详情中显示服务器信息、连接记录、路由冲突、实时流量和历史流量；
- macOS 状态栏菜单支持快速打开窗口、连接或断开配置、查看状态、设置、关于和退出；
- 支持登录后自动启动，关闭主窗口不会自动断开后台 VPN；
- 关于页面支持检查 GitHub Releases 更新，下载前校验发布资产。

## 支持平台

### macOS

macOS 提供多连接能力，并分别构建两个安装包：

- `x86_64`：Intel Mac；
- `arm64`：Apple Silicon Mac。

Apple Silicon 使用 `arm64` 安装包时不需要 Rosetta。当前构建最低支持 macOS 26，开发包使用临时签名，正式发布还需要 Developer ID 签名和公证。

### Windows

Windows 版本使用 Qt 桌面界面、OpenVPN3 Windows Service 和 Wintun/TAP 适配层，支持多条配置同时连接。Windows daemon 与 macOS 使用相同的会话逻辑，Windows agent 为每条会话隔离 TUN、句柄、路由和 DNS；仍需要在 Windows 主机上完成驱动、Service、路由、DNS、通知区域和安装升级的实机验收。

### Android/iOS

移动端暂未提供客户端。后续将分别使用 Android `VpnService` 和 iOS `NEPacketTunnelProvider`，并保持单条活动系统 VPN 的能力边界。

## 路由处理

默认使用兼容模式：发生重叠时保留先建立连接的路由，后建立连接跳过冲突路由；严格模式会拒绝发生冲突的新连接。

连接详情支持配置 IP/CIDR 规则：

- 优先当前连接：发现重叠时提示先断开冲突连接，再重新连接当前连接；
- 排除当前连接：当前连接不安装匹配的服务器路由；
- 策略修改不会热更新已经建立的隧道，需要断开并重新连接；
- 域名后缀规则会参与 macOS 分流 DNS；任意模糊匹配和正则规则不支持。

## 使用方式

安装 macOS `.pkg` 后，打开 `/Applications/OpenVPN.app`：

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

开发环境需要 Qt 6.12、CMake、OpenSSL、asio、fmt、jsoncpp、lz4 和 xxHash。基础测试不需要真实 VPN 配置：

```bash
cmake -S core -B /tmp/openvpn-core-build
cmake --build /tmp/openvpn-core-build --parallel
ctest --test-dir /tmp/openvpn-core-build --output-on-failure
```

生成 macOS 安装包时，使用 `packaging/macos/build-release.sh`。Intel 构建的最后两个参数为 `x86_64` 和 `/usr/local`；Apple Silicon 构建改为 `arm64` 和 `/opt/homebrew`。脚本会构建 core、Qt 应用、daemon 和 helper，运行测试并检查安装包动态库。

更完整的打包说明见[macOS 打包文档](packaging/macos/README.md)，Windows 说明见[Windows 打包文档](packaging/windows/README.md)。

## GitHub Releases

仓库地址：<https://github.com/flyu518/open-vpn>

`.github/workflows/release-macos.yml` 会在推送 `v<版本号>` tag 后，分别构建 Intel 和 Apple Silicon 安装包，生成 SHA-256 校验文件并创建 GitHub Release。发布前需要为仓库配置允许 workflow 写入 `contents` 的权限，并完成正式签名和公证配置。

## 数据和隐私

配置索引和应用副本保存在：

```text
~/Library/Application Support/OpenVPN/
```

密码使用系统钥匙串保存。历史流量只记录时间和上下行字节，不记录访问内容、密码或私钥文本。日志和错误信息会经过脱敏处理。

## 已知限制

- macOS 当前最低支持 macOS 26；正式签名和公证尚未配置；
- Windows 多会话代码和 MSVC/CTest 链路已接入，仍需使用真实 VPN 配置完成 Windows 驱动、Service、路由/DNS 和安装包实机验收；
- Android/iOS 尚未实现；
- 域名后缀分流依赖 VPN 提供 DNS 服务器；任意模糊匹配和正则规则尚未支持；
- 路由策略修改需要断开并重新连接后生效。

协议定义见[VPN 协议 v1](docs/protocol-v1.md)，当前桌面验收范围见[桌面客户端验收记录](docs/desktop-acceptance.md)。
