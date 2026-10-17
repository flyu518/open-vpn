# macOS 打包

当前目录已经提供 launchd 配置和安装脚本：

- `com.openvpn.helper.plist`：以 `com.openvpn.helper` 注册高权限 helper，前台运行并交给 launchd 保活，Unix socket 使用 `staff` 组和受限权限。
- `install-helper.sh`：校验 plist、安装 helper、创建日志目录并加载 launchd 服务。
- `com.openvpn.daemon.plist`：用户权限 daemon 的 launchd 配置，负责给 Flutter 界面提供 `/tmp/openvpn-ui.sock`。
- `install-daemon.sh`：把 daemon 安装到当前用户目录并加载 `gui/<uid>` 服务，不需要 sudo。
- `install-openvpn.sh`：一次安装 OpenVPN.app、macOS helper 和用户 daemon；普通用户执行，只有 helper 安装步骤请求 sudo。
- `build-pkg.sh`：把 OpenVPN.app、daemon、helper 和 launchd 配置打成一个 `.pkg`，用户安装一次后以后直接打开应用即可。

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
4. 提供 arm64 或 universal 的 OpenSSL、fmt、lz4、jsoncpp、asio 和 xxhash 依赖；当前开发机的 `/usr/local` 依赖只用于 x86_64 验证。
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
  /绝对路径/open-vpn/ui/build/macos/Build/Products/Release/OpenVPN.app \
  /tmp/openvpn-core-build-x86/openvpn_macos_agent \
  /tmp/openvpn-core-build-x86/openvpn_daemon
```

交付给其他 macOS 用户时，推荐生成安装包：

```bash
./packaging/macos/build-pkg.sh \
  /绝对路径/open-vpn/ui/build/macos/Build/Products/Release/OpenVPN.app \
  /tmp/openvpn-core-build-x86/openvpn_macos_agent \
  /tmp/openvpn-core-build-x86/openvpn_daemon
```

默认输出到 `packaging/macos/dist/OpenVPN-0.1.0.pkg`。用户双击安装包，输入一次管理员密码，
安装程序会注册高权限 helper 和用户 daemon；以后不需要再执行脚本。当前开发包使用临时签名，
正式分发前还需要使用 Developer ID 对 `.app` 和 `.pkg` 签名并完成 notarization。
