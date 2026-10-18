# 桌面客户端验收记录

本文记录 Qt 桌面版的验收范围、验证结果和已知限制。

## 当前状态与记录范围

产品版本和连接参数统一读取 [project-config.json](../project-config.json)。`1.0.0` 已发布；本页记录平台验收范围与未覆盖项目。后续版本按 [发布操作约定](../RELEASING.md) 执行。

Windows `1.0.0` 安装包已实际升级安装，并完成两条真实配置同时连接和独立断开/重连；core CTest 17/17、Qt CTest 15/15。Mac ARM64 对同一源码提交的 core CTest 19/19、Qt CTest 8/8，完成构建、安装及启动检查，但重装后未重做真实双连接。包哈希、UI 实测范围和未测项见 [Windows 验收记录](releases/1.0.0-windows.md)及 [macOS 验收记录](releases/1.0.0-macos.md)。0.0.14 等历史结果保留在 [Windows 验收说明](windows-acceptance.md)。

以下历史记录按各自测试平台和版本说明，不代表对其他平台或后续版本的验收。

## 历史 macOS 验收记录（0.0.3）

记录环境：Apple Silicon、macOS 26.5.1；该轮使用 x86_64 包在 Rosetta 下验收，arm64 包由原生 Apple Silicon runner 构建。

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

## 历史安装与构建记录

上述 Mac 记录写入时，本机安装版本记为 `0.2.11`；`0.0.3` 的 x86_64 和 arm64 安装包由 GitHub Actions 的原生 runner 负责构建。当前统一版本为 `1.0.0`，其安装包见 GitHub Release。

`.github/workflows/release-macos.yml` 在 tag 触发后按 `third_party/openvpn3.lock` 构建 x86_64/arm64 安装包并上传为 Actions 产物。发布流程由 `.github/workflows/publish-release.yml` 核验版本、源码提交和各平台安装包哈希。

## 已知限制

- Windows 1.0.0 的真实双连接及安装包验收范围见 [Windows 桌面版验收说明](windows-acceptance.md)；不能据此推定其他机器或后续安装包通过。
- Android/iOS 尚未实现。
- macOS 支持构建 x86_64 和 arm64 安装包；1.0.0 Release 提供 arm64 包，Intel Mac 安装未纳入本次验收。
- macOS 当前为临时签名，尚未完成 Developer ID 签名和公证。
- 当前最低 macOS 版本为 26。
- 路由策略不会热更新已建立的隧道；需要按提示断开冲突连接、断开当前连接，再重新连接以接管路由。
- IP/CIDR 优先/排除及域名后缀系统分流 DNS 已接入桌面平台适配层；任意模糊匹配仍不支持。
- 客户端从 GitHub Releases 获取更新；安装包下载前校验资产大小和 SHA-256。

## 验证命令

以下为 Mac 构建目录的命令示例；完整跨平台构建与发布流程见 [RELEASING.md](../RELEASING.md)。

```bash
/绝对路径/cmake --build /tmp/openvpn-release/core
/绝对路径/ctest --test-dir /tmp/openvpn-release/core --output-on-failure

/绝对路径/cmake --build /tmp/openvpn-release/qt
QT_QPA_PLATFORM=offscreen \
/绝对路径/ctest --test-dir /tmp/openvpn-release/qt --output-on-failure

python3 -m unittest discover -s packaging/macos/tests -v
```
