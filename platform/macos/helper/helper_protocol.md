# macOS VPN Helper 协议

helper 由高权限账户运行，通过 Unix socket `/var/run/vpn.sock` 接收
OpenVPN3 Core 的 TUN、旁路路由和销毁请求。每个会话使用
`进程 PID + VPN_DESKTOP_SESSION_ID` 作为会话键；同一桌面进程中的多个 OpenVPN3
客户端不会再共享同一个 utun 状态。

- `POST /tun-setup`：根据会话参数创建并配置 utun。
- `POST /add-bypass-route`：让 VPN 服务器地址继续走物理网络。
- `POST /tun-destroy`：请求体携带 `pid` 和 `session_name`，销毁指定会话的 utun、路由和 DNS 状态。
- `GET /tun-destroy`：兼容旧 command agent，按当前 HTTP 客户端记录销毁会话。

当前构建使用 OpenVPN3 官方 macOS agent 实现，但将 agent 名称固定为
`vpn`，不连接或复用 OpenVPN Connect 的私有 `agent_ovpnconnect.sock`。

helper 的 launchd 配置位于 `packaging/macos/com.vpn.helper.plist`，本地安装脚本位于
`packaging/macos/install-helper.sh`。离线测试只验证会话和路由状态机，不启动高权限进程。

helper 当前将 Unix socket 限制为 `staff` 组可读写，并拒绝 root 或非 helper 有效组的
peer credentials；正式发布前仍需加入应用签名/调用方鉴权，不能仅依赖本地组权限作为
生产安全边界。
