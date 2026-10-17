# macOS 打包

当前目录已经提供 launchd 配置和安装脚本：

- `com.openvpn.helper.plist`：以 `com.openvpn.helper` 注册高权限 helper，前台运行并交给 launchd 保活，Unix socket 使用 `staff` 组和受限权限。
- `install-helper.sh`：校验 plist、安装 helper、创建日志目录并加载 launchd 服务。
- `com.openvpn.daemon.plist`：用户权限 daemon 的 launchd 配置，负责给 Qt 桌面界面提供 `/tmp/openvpn-ui.sock`。
- `install-daemon.sh`：把 daemon 安装到当前用户目录并加载 `gui/<uid>` 服务，不需要 sudo。
- `install-openvpn.sh`：一次安装 OpenVPN.app、macOS helper 和用户 daemon；普通用户执行，只有 helper 安装步骤请求 sudo。
- `build-pkg.sh`：把 OpenVPN.app、daemon、helper 和 launchd 配置打成一个 `.pkg`，用户安装一次后以后直接打开应用即可。
- `com.openvpn.component.plist`：固定 OpenVPN.app 安装到 `/Applications`，禁止 Installer 把它重定位回构建目录。

构建出 `openvpn_macos_agent` 后，可以使用下面的命令安装本地开发版本：

```bash
sudo ./packaging/macos/install-helper.sh \
  /绝对路径/openvpn-core-build/openvpn_macos_agent
```

脚本会修改系统的 `/Library/PrivilegedHelperTools`、`/Library/LaunchDaemons` 和
`/var/log/openvpn`，因此只在确认目标主机后执行。卸载或升级前应先执行：

```bash
sudo launchctl bootout system/com.openvpn.helper
sudo rm /Library/LaunchDaemons/com.openvpn.helper.plist
sudo rm /Library/PrivilegedHelperTools/com.openvpn.helper
```

正式发布包还需要完成以下工作：

1. 为桌面应用和 helper 使用同一发行证书分别签名，并配置 hardened runtime、notarization 和权限说明。
2. 将 helper 的本地 IPC 改为带应用调用方校验的协议。当前开发版已经限制 socket 组权限并传递独立会话标识，但不能直接作为最终生产安全边界。
3. 将 Keychain 凭据存储接入 profile 生命周期，日志继续经过脱敏层，不能把密码写入日志或崩溃报告。
4. 使用对应架构的 OpenSSL、fmt、lz4、jsoncpp、asio 和 xxhash 依赖；Intel 使用 `/usr/local`，Apple Silicon 使用 `/opt/homebrew`，不能交叉复用另一架构的 Homebrew 库。
5. 使用真实的两个 `.ovpn` profile 验证同时连接、单独断开、路由旁路、DNS 恢复和 helper 崩溃清理。

当前脚本只负责 helper 的安装和生命周期，不会替代真实 utun、路由、DNS 以及 IPC 鉴权实现。

构建出 `openvpn_daemon` 后，可以用下面的命令让用户 daemon 登录后自动运行：

```bash
./packaging/macos/install-daemon.sh \
  /绝对路径/openvpn-core-build/openvpn_daemon
```

用户 daemon 不拥有修改系统路由的权限；它通过 OpenVPN3 会话和已安装的高权限 helper
协作完成连接。卸载时执行：

```bash
launchctl bootout gui/$(id -u)/com.openvpn.daemon
rm "$HOME/Library/LaunchAgents/com.openvpn.daemon.plist"
rm -rf "$HOME/Library/Application Support/OpenVPN"
```

开发阶段仍可使用一次性安装脚本：

```bash
./packaging/macos/install-openvpn.sh \
  /tmp/openvpn-qt-build/openvpn_qt.app \
  /tmp/openvpn-core-build/openvpn_macos_agent \
  /tmp/openvpn-core-build/openvpn_daemon
```

交付给其他 macOS 用户时，推荐生成安装包：

```bash
./packaging/macos/build-pkg.sh \
  /tmp/openvpn-qt-build/openvpn_qt.app \
  /tmp/openvpn-core-build/openvpn_macos_agent \
  /tmp/openvpn-core-build/openvpn_daemon
```

默认输出名为 `packaging/macos/dist/OpenVPN-<版本号>.pkg`。用户双击安装包，输入一次管理员密码，
安装程序会注册高权限 helper 和用户 daemon；以后不需要再执行脚本。当前开发包使用临时签名，
正式分发前还需要使用 Developer ID 对 `.app` 和 `.pkg` 签名并完成 notarization。
当前桌面版不启用 App Sandbox，这是为了访问用户配置目录和 VPN Unix socket；正式发布必须使用
Developer ID、Hardened Runtime，并继续限制 helper IPC 和 Keychain 凭据访问范围。

## 托盘、自启和 GitHub 发布

安装完成后，直接打开 `/Applications/OpenVPN.app` 即可显示 macOS 状态栏图标。关闭窗口只会隐藏主窗口，
托盘菜单仍可打开 OpenVPN、查看连接、连接/断开配置、打开设置、关于或退出程序；版本检查统一放在“关于 OpenVPN”中。

“开机自启”由 Qt 应用写入当前用户的 `~/Library/LaunchAgents/com.openvpn.desktop.plist`，不需要 sudo；它只负责启动
桌面 UI，不替代 `com.openvpn.daemon` 或高权限 helper。更新 UI 本身不需要重新安装 helper，只有发布包里的
helper/daemon 变化时才重新执行完整安装包。

发布时先构建 Qt 应用并部署 Qt 运行时：

```bash
OPENVPN_CMAKE=/绝对路径/cmake packaging/macos/build-release.sh \
  /绝对路径/Qt/6.12.0/macos \
  /tmp/openvpn-0.0.1-release \
  0.0.1 \
  /绝对路径/xxHash \
  x86_64 \
  /usr/local
```

完整流程会从同一源码构建 Qt、helper 和 daemon，运行测试，部署 Qt 插件及服务动态库，再生成
`/tmp/openvpn-0.0.1-release/OpenVPN-0.0.1-macos-x86_64.pkg`。Apple Silicon 本地构建时使用 `arm64 /opt/homebrew`，输出文件名会相应变为 `OpenVPN-0.0.1-macos-arm64.pkg`。发布前检查的范围包括 socket 名称、版本、动态库加载路径和架构；不要把历史 helper 产物混入当前安装包。

当前随包动态库最低要求 macOS 26，发布流程分别构建 x86_64 和 arm64；开发版本采用临时签名，未完成 Developer ID 签名公证。

应用只在“关于 OpenVPN”中检查 GitHub Releases，下载前会确认并校验发行资产的大小和 SHA-256，安装由系统安装器再次授权。
