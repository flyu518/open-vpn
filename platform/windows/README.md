# Windows 平台适配器

Windows helper 入口已经接入 OpenVPN3 官方 Windows agent 的 Service、命名管道和
Wintun/TAP 构建边界，并使用 `openvpn` agent 名称与 OpenVPN Connect 隔离。

`core/tools/windows_daemon_main.cpp` 现在提供用户权限的 OpenVPN daemon，使用
`\\.\pipe\openvpn-ui` 接受与 macOS Unix socket 相同的一行 JSON 命令，凭据使用
Windows Credential Manager 保存。

目录中的 `adapter_contract.h` 是能力契约测试用的占位入口，`adapter/` 是离线路由状态模型；
生产连接通过共享 OpenVPN3 会话引擎和下述 Windows agent 完成，不调用占位连接函数。

Windows daemon 的会话上限为 `0`，每条配置会话独立管理自己的 TUN、进程句柄、事件、路由和
DNS 状态。普通三层 TUN 默认使用每会话独立的 Wintun；显式驱动或 TAP 按配置处理。
生成的 Windows agent 把同一会话跨命名管道的 `tun-open`/`tun-setup` 请求关联到该会话的
网卡租约，避免多条连接互相覆盖。Windows 与 macOS 使用相同的多连接逻辑和 UI 交互。

对 `third_party/openvpn3.lock` 固定的官方 OpenVPN3 Windows 源码分析确认，官方 `MyListener`
原本只有一套 TUN、客户端进程句柄和销毁事件，并在 `/tun-open` 前销毁上一条连接；这正是原始
实现只能单连接的原因。本项目保留官方客户端的 JSON/命名管道协议，只在构建时生成按会话隔离
资源的 agent 源码，因此不需要修改 macOS 交互或 Windows UI。

本机 Windows 0.0.14 已完成标准 EXE 升级、真实双配置同时连接及独立断开/重连，
本地核心用例也已检查分流 DNS、IPv6 所有权和异常退出清理；证据边界与未测项目见
[Windows 验收记录](../../docs/windows-acceptance.md)。每个候选仍须在目标 Windows 主机回归，
可使用 `platform/windows/tests/windows_multisession_smoke.ps1` 检查 A/B 连接与清理。

产品版本和连接参数读取根目录 [project-config.json](../../project-config.json)，
跨平台交接和发布统一遵循 [RELEASING.md](../../RELEASING.md)。
