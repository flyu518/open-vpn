# macOS VPN Helper 协议

helper 由高权限账户运行，通过 Unix socket `/var/run/vpn.sock` 接收
OpenVPN3 Core 的 TUN、旁路路由和销毁请求。

- `POST /tun-setup`：根据会话参数创建并配置 utun。
- `POST /add-bypass-route`：让 VPN 服务器地址继续走物理网络。
- `GET /tun-destroy`：销毁会话的 utun、路由和 DNS 状态。

当前构建使用 OpenVPN3 官方 macOS agent 实现，但将 agent 名称固定为
`vpn`，不连接或复用 OpenVPN Connect 的私有 `agent_ovpnconnect.sock`。

helper 的 launchd 配置位于 `packaging/macos/com.vpn.helper.plist`，本地安装脚本位于
`packaging/macos/install-helper.sh`。离线测试只验证会话和路由状态机，不启动高权限进程。

注意：上游 agent 当前把 Unix socket 设为宽松模式，且客户端放行逻辑还没有接入本项目的
调用方校验。这个 helper 只能作为开发联调入口，完成 IPC 鉴权前不能当作生产安全边界。
