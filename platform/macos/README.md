# macOS 平台适配器

当前版本已经接入 OpenVPN3 官方 macOS tun agent 的本地生成构建，并将其单会话状态改为
按“进程 PID + `VPN_DESKTOP_SESSION_ID`”隔离：每个会话拥有独立 utun、路由清理和远端
服务器旁路路由。helper 在建立隧道前执行共享路由策略：全隧道与其他活动会话互斥，
兼容模式跳过后连接者的重叠路由，严格模式拒绝重叠。

`core/CMakeLists.txt` 会在构建目录生成两个基于已锁定 OpenVPN3 源码的补丁头文件，分别
为 command agent 增加会话标识，为客户端 transport 配置读取桌面会话标识；不会修改
`third_party/openvpn3` 原始目录。

macOS 已有双连接和界面验收的 [历史记录](../../docs/desktop-acceptance.md)，但不替代新候选测试。
目标主机仍须验证当前候选的 utun/DNS、安装包和对应架构依赖；正式签名、公证与调用方鉴权
也必须单独审查，不能因双连接测试通过就宣称完成安全验收。

产品版本和连接参数统一读取 [project-config.json](../../project-config.json)，
更新和跨平台发布遵循根目录 [RELEASING.md](../../RELEASING.md)，不改变既有 macOS 操作流程。
