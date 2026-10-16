# macOS 打包

当前目录已经提供 launchd 配置和安装脚本：

- `com.vpn.helper.plist`：以 `com.vpn.helper` 注册高权限 helper，前台运行并交给 launchd 保活。
- `install-helper.sh`：校验 plist、安装 helper、创建日志目录并加载 launchd 服务。

构建出 `vpn_macos_agent` 后，可以使用下面的命令安装本地开发版本：

```bash
sudo ./packaging/macos/install-helper.sh \
  /绝对路径/vpn-core-build/vpn_macos_agent
```

脚本会修改系统的 `/Library/PrivilegedHelperTools`、`/Library/LaunchDaemons` 和
`/var/log/vpn`，因此只在确认目标主机后执行。卸载或升级前应先执行：

```bash
sudo launchctl bootout system/com.vpn.helper
sudo rm /Library/LaunchDaemons/com.vpn.helper.plist
sudo rm /Library/PrivilegedHelperTools/com.vpn.helper
```

正式发布包还需要完成以下工作：

1. 为桌面应用和 helper 使用同一发行证书分别签名，并配置 hardened runtime、notarization 和权限说明。
2. 将 helper 的本地 IPC 改为带调用方校验的协议。上游 agent 当前使用宽松的 Unix socket 模式，不能直接作为生产安全边界。
3. 将 Keychain 凭据存储接入 profile 生命周期，日志继续经过脱敏层，不能把密码写入日志或崩溃报告。
4. 提供 arm64 或 universal 的 OpenSSL、fmt、lz4、jsoncpp、asio 和 xxhash 依赖；当前开发机的 `/usr/local` 依赖只用于 x86_64 验证。
5. 使用真实的两个 `.ovpn` profile 验证同时连接、单独断开、路由旁路、DNS 恢复和 helper 崩溃清理。

当前脚本只负责 helper 的安装和生命周期，不会替代真实 utun、路由、DNS 以及 IPC 鉴权实现。
