# vpn

正在开发中的跨平台 OpenVPN 桌面客户端。

第一版目标平台是 Windows 和 macOS，支持同时建立多个相互独立的 OpenVPN 连接。Android 和 iOS 暂不实现，只保留后续扩展所需的接口。

## 目录边界

- `core/`：共享 C++ 契约、会话策略和 OpenVPN3 集成
- `platform/`：Windows/macOS 适配层，以及未来移动端适配边界
- `ui/`：Flutter 桌面界面
- `schemas/`：版本化的进程通信和状态契约

`open-vpn/` 目录中的 OpenVPN Connect 安装包只作为分析参考。本项目不能加载它私有的 Electron 文件或原生扩展。

## 基础构建

在工作区根目录执行：

```bash
cmake -S vpn/core -B /tmp/vpn-core-build
cmake --build /tmp/vpn-core-build
ctest --test-dir /tmp/vpn-core-build --output-on-failure
```

基础测试不需要 VPN 配置，也不会发起网络连接。

## 当前验证状态

- 共享 C++ 核心、OpenVPN3 离线 profile 解析和多会话对象已可测试。
- macOS/Windows 路由生命周期模型已可离线测试。
- macOS helper 已按“进程 PID + 独立会话标识”管理多个 utun，并在 helper 侧执行全隧道/重叠路由校验；`packaging/macos/` 已提供 launchd 配置和本地安装脚本。
- Windows Wintun/Service 需要 Windows 主机完成运行验证。
- Flutter SDK 当前未安装，中文桌面界面源码已写入但尚未运行 Flutter 测试。

macOS 当前可以先做不联网的 profile 检查：

```bash
/tmp/vpn-core-build-x86/vpn_smoke_cli --offline \
  vpn/tmp/profile-a.ovpn vpn/tmp/profile-b.ovpn
```

真实连接入口会在终端隐藏读取用户名和密码；在 helper 安装、签名和 IPC 鉴权完成前，
不要输入真实凭据。

macOS 本地 UI daemon 可以这样启动：

```bash
/tmp/vpn-core-build-x86/vpn_daemon --socket /tmp/vpn-ui.sock
```

它使用用户权限 Unix socket 接受界面命令，并把每个 OpenVPN3 会话的状态事件广播给
已连接的桌面界面。

如果 `third_party/openvpn3` 已经存在，CMake 会额外编译 OpenVPN3 Core
离线测试。当前开发机的 `/usr/local` 依赖是 x86_64，使用 Rosetta 验证时执行：

```bash
PYTHONPATH=/private/tmp/vpn-cmake /private/tmp/vpn-cmake/bin/cmake \
  -S vpn/core -B /tmp/vpn-core-build-x86 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DOPENVPN3_SOURCE_DIR=/Users/cuikai/www_python/vpn/third_party/openvpn3 \
  -DXXHASH_ROOT_DIR=/private/tmp/xxHash-0.8.4
PYTHONPATH=/private/tmp/vpn-cmake /private/tmp/vpn-cmake/bin/cmake \
  --build /tmp/vpn-core-build-x86
(cd /tmp/vpn-core-build-x86 && ctest --output-on-failure)
```

正式 arm64 发布包需要 arm64 或 universal 的 OpenSSL、fmt、lz4 和 xxhash
依赖；不能直接复用当前 x86_64 Homebrew 库。
