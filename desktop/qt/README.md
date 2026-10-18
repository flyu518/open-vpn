# OpenVPN Qt 桌面版

这里是 Windows/macOS 桌面主界面的 Qt 6/QML 实现。OpenVPN3 核心、daemon、helper、凭据存储和路由适配层仍然使用项目现有的 C++ 实现，Qt 界面通过统一的本地 IPC 协议访问它们。

版本和共享连接参数读取根目录 [project-config.json](../../project-config.json)，不得在 Qt 构建参数中另设平台版本。
旧构建目录的版本缓存冲突时，加 `-UOPENVPN_APP_VERSION` 重新配置；新候选应使用新的构建目录。
更新及验收统一遵循 [RELEASING.md](../../RELEASING.md)，Windows 构建见 [Windows 打包说明](../../packaging/windows/README.md)。

## macOS 构建

先安装 Qt 6.8 或更新版本、CMake 3.21 或更新版本，并确保 `qt-cmake`、`macdeployqt` 可用。先把 Qt SDK 根目录设置为 `QT_ROOT`：

```text
QT_ROOT=/Qt/6.8.3/macos  # 替换为实际安装的 Qt 6.8+ macOS SDK 路径
```

构建和测试：

```bash
cmake -S desktop/qt -B /tmp/openvpn-qt-build \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/openvpn-qt-build --parallel
ctest --test-dir /tmp/openvpn-qt-build --output-on-failure
```

Qt 应用位于：

```text
/tmp/openvpn-qt-build/openvpn_qt.app
```

发布前需要把 Qt 框架和 QML 运行时部署到应用包：

```bash
"$QT_ROOT/bin/macdeployqt" \
  /tmp/openvpn-qt-build/openvpn_qt.app \
  -qmldir=desktop/qt/qml
```

开发阶段可以使用 `QT_QPA_PLATFORM=offscreen` 做启动级检查；真实的状态栏点击、VPN 连接、路由和 DNS 仍需在 macOS/Windows 图形环境以及已安装 daemon/helper 的机器上验证。

## 当前实现

- Qt/QML 主窗口、连接列表和紧凑连接卡片；
- 配置索引读取、导入、导出、重命名和拖动排序；
- Unix socket（macOS）与 named pipe（Windows）IPC 客户端；
- 连接、断开、自动读取安全存储凭据、认证失败后输入凭据；
- 详情、兼容/严格路由冲突策略和冲突提示；
- macOS 状态栏、Windows 通知区域菜单、关闭窗口保留后台运行；
- 开机自启设置；
- 关于页面中的 GitHub Releases 版本检查。

Android/iOS 不复用桌面托盘和窗口代码，后续继续沿用共享协议与核心模型，分别接入系统 VPN 扩展。
