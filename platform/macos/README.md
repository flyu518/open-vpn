# macOS 平台适配器

当前版本已经接入 OpenVPN3 官方 macOS tun agent 的本地生成构建，并将其单会话状态改为
按“进程 PID + `VPN_DESKTOP_SESSION_ID`”隔离：每个会话拥有独立 utun、路由清理和远端
服务器旁路路由。helper 还会在建立隧道前执行共享路由策略，拒绝第二个全隧道或重叠网段。

`core/CMakeLists.txt` 会在构建目录生成两个基于已锁定 OpenVPN3 源码的补丁头文件，分别
为 command agent 增加会话标识，为客户端 transport 配置读取桌面会话标识；不会修改
`third_party/openvpn3` 原始目录。

仍需在目标 macOS 主机完成：真实 helper 安装、签名/notarization、调用方鉴权、真实
utun/DNS 双连接冒烟测试，以及 arm64/universal 依赖验证。
