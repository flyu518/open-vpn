# OpenVPN

正在开发中的跨平台 OpenVPN 桌面客户端。

第一版目标平台是 Windows 和 macOS，支持同时建立多个相互独立的 OpenVPN 连接。Android 和 iOS 暂不实现，只保留后续扩展所需的接口。

## 目录边界

- `core/`：共享 C++ 契约、会话策略和 OpenVPN3 集成
- `platform/`：Windows/macOS 适配层，以及未来移动端适配边界
- `ui/`：Flutter 桌面界面
- `schemas/`：版本化的进程通信和状态契约

项目外部已有的 OpenVPN Connect 安装包只作为分析参考。本项目不能加载它私有的 Electron 文件或原生扩展。

## 基础构建

在工作区根目录执行：

```bash
cmake -S core -B /tmp/openvpn-core-build
cmake --build /tmp/openvpn-core-build
(cd /tmp/openvpn-core-build && ctest --output-on-failure)
```

基础测试不需要 VPN 配置，也不会发起网络连接。

## 当前验证状态

- 共享 C++ 核心、OpenVPN3 离线 profile 解析和多会话对象已可测试。
- macOS/Windows 路由生命周期模型已可离线测试。
- macOS helper 已按“进程 PID + 独立会话标识”管理多个 utun，并在 helper 侧执行全隧道/重叠路由校验；`packaging/macos/` 已提供 launchd 配置和本地安装脚本。
- Windows helper 已接入官方 Service/Wintun 构建边界；仍需要 Windows 主机完成驱动、Service、路由/DNS 和多会话运行验证。
- Flutter 3.47.5 / Dart 3.13.4 已安装到 `.tools/flutter`；Flutter 测试、静态分析和 macOS 桌面构建已通过，Windows 构建仍需 Windows 主机。
- macOS Flutter 界面已接入文件选择器、临时凭据输入、Unix socket daemon 响应和事件；本地只持久化
  profile 路径、显示名称、路由策略和不含敏感信息的活动摘要，不保存 profile 内容或密码，启动时会按路径重新读取配置。
- 每个连接卡片的“查看详情”中会展示该配置自己的连接次数、最近连接和最近断开时间；这些是轻量活动摘要，
  会随配置索引保存，不另设占空间的全局连接记录页，也不会保存密码、证书或完整日志。
- 连接列表从上到下对应优先级 1、2、3……，数字越小越优先；拖动卡片会同步更新优先级。
- Windows Flutter 界面已经接入用户 daemon 的 named pipe 客户端，CMake 也提供
  `openvpn_windows_daemon`；Wintun、Service、路由/DNS 的真实运行验证仍需在 Windows 主机完成，
  macOS 开发机不会把它伪装成已连接。

macOS 当前可以先做不联网的 profile 检查：

```bash
/tmp/openvpn-core-build-x86/openvpn_smoke_cli --offline \
  tmp/profile-a.ovpn tmp/profile-b.ovpn
```

真实连接入口会在终端隐藏读取用户名和密码；在 helper 安装、签名和 IPC 鉴权完成前，
不要输入真实凭据。

macOS 本地 UI daemon 可以这样启动：

```bash
/tmp/openvpn-core-build-x86/openvpn_daemon --socket /tmp/openvpn-ui.sock
```

它使用用户权限 Unix socket 接受界面命令，并把每个 OpenVPN3 会话的状态事件广播给
已连接的桌面界面。

如果希望登录后自动启动 UI daemon，可以安装用户级 launchd 服务：

```bash
packaging/macos/install-daemon.sh \
  /tmp/openvpn-core-build-x86/openvpn_daemon
```

给最终用户交付时不需要让他们执行上面的脚本，可以生成一次性 macOS 安装包：

```bash
packaging/macos/build-pkg.sh \
  ui/build/macos/Build/Products/Release/OpenVPN.app \
  /tmp/openvpn-core-build-x86/openvpn_macos_agent \
  /tmp/openvpn-core-build-x86/openvpn_daemon
```

用户双击 `packaging/macos/dist/OpenVPN-0.1.1.pkg` 安装并输入一次管理员密码即可；安装程序会注册
helper 和 daemon，之后直接打开 `/Applications/OpenVPN.app`。开发包使用临时签名，正式对外分发还需要
Developer ID 签名和 notarization。

如果 `third_party/openvpn3` 已经存在，CMake 会额外编译 OpenVPN3 Core
离线测试。当前开发机的 `/usr/local` 依赖是 x86_64，使用 Rosetta 验证时执行：

```bash
PYTHONPATH=/private/tmp/vpn-cmake /private/tmp/vpn-cmake/bin/cmake \
  -S core -B /tmp/openvpn-core-build-x86 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DOPENVPN3_SOURCE_DIR=/Users/cuikai/www_python/open-vpn/third_party/openvpn3 \
  -DXXHASH_ROOT_DIR=/private/tmp/xxHash-0.8.4
PYTHONPATH=/private/tmp/vpn-cmake /private/tmp/vpn-cmake/bin/cmake \
  --build /tmp/openvpn-core-build-x86
(cd /tmp/openvpn-core-build-x86 && ctest --output-on-failure)
```

正式 arm64 发布包需要 arm64 或 universal 的 OpenSSL、fmt、lz4 和 xxhash
依赖；不能直接复用当前 x86_64 Homebrew 库。
