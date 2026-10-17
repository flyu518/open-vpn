# 桌面客户端验收记录

本文记录 Qt 桌面版的验收范围、验证结果和已知限制。

## 当前版本

版本：`0.2.13`。

本机环境：Apple Silicon、macOS 26.5.1；本轮本机使用 x86_64 包在 Rosetta 下验收，arm64 包由原生 Apple Silicon runner 构建。

## 已验证功能

- Core CTest：16/16 通过。
- Qt CTest：6/6 通过，包含存储、IPC、控制器、QML、托盘和历史流量。
- macOS Cocoa 原生界面：当前主流程及弹框回归通过；托盘定向测试 5/5 通过。
- macOS 打包依赖：102 个 Mach-O 文件检查通过，非系统动态库随包提供。
- 双 VPN 实测：两条配置可同时连接，计时、上下行流量和独立断开均已验证。
- 保存密码：使用 macOS 钥匙串自动连接，认证失败时重新请求凭据。
- 配置持久化：导入后的应用内副本独立于原 `.ovpn` 文件，原文件删除不影响使用；副本哈希和权限已检查。
- 冲突路由：卡片显示冲突连接名称，详情显示双方网段、跳过/拒绝结果和手动接管步骤。
- 路由策略：详情中的“编辑路由策略”可以保存兼容/严格模式，并支持 IP/CIDR 的当前连接优先、排除；已连接配置会提示断开并重新连接后生效。
- 历史流量：详情显示最近 1 小时/24 小时曲线，历史文件按配置隔离保存。
- 状态栏菜单：打开主窗口、连接/断开、设置、关于、退出均有菜单入口；菜单项使用“关于”和“退出”短标签。
- 自启动、关闭窗口保留后台连接、更新检查入口均已做 macOS 验证。

## 当前发布包

本机当前安装版本仍为 `0.2.11`；`0.2.13` 的 x86_64 安装包已完成构建和载荷验证，arm64 安装包由 GitHub Actions 的原生 runner 负责构建。

发布流程由 `.github/workflows/release-macos.yml` 管理；推送 `v<版本号>` tag 后，GitHub Actions 会按 `third_party/openvpn3.lock` 拉取 OpenVPN3，分别构建 x86_64/arm64 安装包，生成 SHA-256 并上传 Release。

## 已知限制

- Windows 没有实机验收；当前 Windows agent 只允许一条活动连接，这是保护性预览限制，具体原因和验证步骤见 [Windows 桌面版验收说明](windows-acceptance.md)。
- Android/iOS 尚未实现。
- macOS 发布流程分别提供 x86_64 和 arm64 两个安装包；两种架构的 GitHub Actions 构建尚未在本机同时实跑。
- macOS 当前为临时签名，尚未完成 Developer ID 签名和公证。
- 当前最低 macOS 版本为 26。
- 路由策略不会热更新已建立的隧道；需要按提示断开冲突连接、断开当前连接，再重新连接以接管路由。
- IP/CIDR 优先/排除已接入 macOS helper；域名后缀已接入 macOS 分流 DNS，任意模糊匹配仍不支持。
- GitHub 当前如果没有可访问的正式 Release，客户端会显示暂无发行版；不会伪造更新结果。

## 验证命令

```bash
/绝对路径/cmake --build /tmp/openvpn-release/core
/绝对路径/ctest --test-dir /tmp/openvpn-release/core --output-on-failure

/绝对路径/cmake --build /tmp/openvpn-release/qt
QT_QPA_PLATFORM=offscreen \
/绝对路径/ctest --test-dir /tmp/openvpn-release/qt --output-on-failure

python3 -m unittest discover -s packaging/macos/tests -v
```
