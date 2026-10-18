# Windows 多配置同时连接设计

日期：2026-10-18

> 历史设计快照：下文“背景”描述改造前的单连接状态，方案和测试入口是当时的设计，不是当前能力或交付结果。当前 Windows 已有多连接及标准 EXE 实测记录，见 [Windows 验收](../../windows-acceptance.md)；后续操作统一遵循 [RELEASING.md](../../../RELEASING.md)。保留原设计用于追踪决策，不按旧方案重新执行。

## 背景

项目的 macOS 版本已经支持多条 OpenVPN 配置同时连接，Qt 界面按会话分别展示状态、计时、流量、路由冲突和断开操作。Windows 版本虽然共享同一套桌面协议和 UI，但 `WindowsDaemon` 当前把 `SessionController` 的活动会话上限固定为 `1`，并且 Windows agent 的 TUN、旁路路由、分流路由和 DNS 生命周期尚未按会话隔离。

目标是在不改变 macOS 代码和交互的前提下，让 Windows 遵循相同的桌面多连接语义：多个配置可以并行连接；每条连接拥有独立的 native session、虚拟网卡、路由和清理生命周期；Qt 不增加 Windows 特例。

## 目标与验收标准

### 目标

- Windows daemon 不再主动拒绝第二条活动连接。
- 每条连接使用唯一且可追踪的 Windows agent session key。
- 每条连接拥有独立的 Wintun/TAP 生命周期、远端服务器旁路路由、VPN 路由策略和 watchdog 清理范围。
- Windows 的路由行为与 macOS 一致：全隧道互斥；严格模式拒绝重叠；兼容模式跳过冲突路由并回传诊断；域名规则只修改当前会话的 DNS 配置。
- Windows UI 使用现有跨平台协议和 Qt 会话模型，不修改 macOS 交互，不引入 Windows 专用提示或按钮状态。
- 连接失败、断开、daemon 重启或 service 异常时，只清理对应会话；残余资源能够通过会话或进程 watchdog 清理。

### 验收标准

1. 两个网段不重叠的配置可以同时进入 `connected`，两个会话的状态、计时和流量独立更新。
2. 断开 A 后，B 仍保持连接并继续产生流量；A 的虚拟网卡、路由和 DNS 不残留。
3. 相同路由在兼容模式下只由先建立的会话保留，后建立会话进入连接状态并回传冲突诊断；严格模式下后建立会话失败且不影响先建立会话。
4. 两个全隧道配置不能同时接管默认路由，错误语义与 macOS 一致。
5. 断开顺序、连接取消、认证失败、网络断开、daemon 重启和 Windows service 停止不会导致其他活动会话被误删或替换。
6. Windows x64 实机能够观察到两个独立的 Wintun/TAP 接口及各自的路由/DNS 结果；基础测试和 Qt 测试全部通过。

## 方案选择

### 方案 A：复用 OpenVPN3 Windows agent，补齐按会话隔离（采用）

保留现有 `OpenVPN3Session`、Windows Service 和 JSON named pipe 架构。daemon 取消 `max_active_sessions = 1`，每条 native session 通过现有的 `VPN_DESKTOP_SESSION_ID` 传入唯一会话名。构建时从固定版本的 OpenVPN3 源码生成 Windows agent 补丁，把 agent 内原先按单实例管理的状态改为按 `pid + sessionId` 管理。

优点是延续现有 macOS 的设计边界和 OpenVPN3 的连接实现，UI 和 daemon 协议无需分叉；缺点是需要维护对固定 OpenVPN3 版本的源码补丁，并通过 Windows 实机确认 Wintun、路由和 DNS 的实际行为。

### 方案 B：每条连接启动独立 agent/service 进程

为每个 profile 创建独立高权限进程和命名管道。该方案隔离性强，但会引入多个 service 实例、安装/升级/崩溃恢复和权限管理复杂度，也无法自然复用当前单一 Windows Service 的生命周期。

### 方案 C：自行实现 Windows TUN、路由和 DNS 层

绕过 OpenVPN3 agent，在项目中直接使用 Wintun API 和 Win32 网络接口管理资源。该方案控制力最大，但会重复实现 OpenVPN3 已有的系统网络生命周期，改动面和维护风险都明显高于当前需求。

## 架构设计

### 会话身份

`SessionController` 仍以 `sessionId` 作为 UI 和 daemon 协议中的主键。`OpenVPN3Session::setProfile()` 将安全清洗后的 session id 写入 OpenVPN3 配置；Windows agent 使用当前进程 PID 与该 session id 组成资源 key，避免不同 daemon 进程或不同配置之间发生碰撞。

Windows agent 中所有会话资源都必须以该 key 索引：

- Wintun/TAP setup、句柄和接口状态；
- 远端 VPN 服务器旁路路由及其撤销命令；
- 当前会话最终安装的路由策略；
- 当前会话的 DNS 配置；
- watchdog 与进程存活关系。

断开时只查找并销毁对应 key。进程退出时 watchdog 枚举同一 PID 下的所有 key，逐一清理，不再只清理最后一个 TUN。

### Windows agent 补丁

新增 Windows agent 的 configure-time 源码生成脚本，参照 macOS 的 `generate_multisession_agent.cmake`，不直接修改 `third_party/openvpn3`。脚本必须对固定版本的目标片段做必需匹配，片段缺失时 CMake 配置失败，避免上游源码变化后静默生成错误 agent。

补丁后的 agent 负责：

1. 为每个 session key 创建独立的 TUN setup，并把唯一基础名称传给 Wintun/TAP 创建逻辑。
2. 根据 `VPN_DESKTOP_ROUTE_MODE` 和 `VPN_DESKTOP_ROUTE_RULES` 计算当前会话的路由策略。
3. 在安装路由前与其他活动 session 的策略比较：全隧道互斥；严格模式返回错误；兼容模式删除冲突路由并输出结构化可解析日志。
4. 对当前会话应用域名规则和 DNS 服务器，销毁时只撤销当前接口的 DNS 状态。
5. 对服务器 endpoint 的旁路路由做按 session key 的引用计数/撤销，避免多个配置访问同一 endpoint 时互相删除。
6. 对 watchdog、service stop 和 TUN setup 失败执行幂等清理。

### daemon 与 UI

- `WindowsDaemon` 构造 `SessionController` 时使用默认无限活动会话配置，不再传入 `1`。
- `SessionController` 的跨平台行为保持不变，包括操作锁、并发 connect 的串行化、失败中的 native session 名额占用和独立事件广播。
- 不新增 Windows 专用错误文案。旧的单连接错误路径和相关测试改为多连接成功/隔离测试。
- Qt 的 `EngineClient`、`AppController`、`SessionListModel`、QML 卡片、托盘菜单和详情页不改 macOS 现有交互；如果现有代码中存在仅针对单连接错误的展示，应删除该分支，让 agent/daemon 的通用错误按现有诊断流程显示。

## 数据流与失败处理

连接 A/B 的流程为：

1. Qt 分别发送两个 `connect` 请求，daemon 为每个请求创建独立 `OpenVPN3Session`。
2. 每个 session 将唯一身份和路由策略传给 OpenVPN3 client；OpenVPN3 Windows client 请求 agent 创建对应的 Wintun/TAP。
3. agent 以 session key 完成接口、DNS、旁路路由和分流路由配置，并把路由冲突信息写入事件日志。
4. daemon 将事件转换为对应 session 的 snapshot/notification；Qt 只更新相同 `sessionId` 的卡片。
5. disconnect A 先从 daemon 记录移出 A，再停止 A 的 native session；agent 只销毁 A 的资源，B 的 worker 和资源不受影响。

如果第二个接口、路由或 DNS 配置失败，A 必须保持活动；B 进入 `failed` 并保留可读诊断。任何 agent 失败都不能回退为“复用第一个 TUN”，也不能清理其他 session。所有外部日志进入 UI 前继续经过现有脱敏和长度限制。

## 测试设计

### 跨平台单元/集成测试

先增加失败测试，再实现代码：

- `SessionController` 默认配置同时接受两个及以上会话；
- 两个会话的 snapshot、流量和状态互不覆盖；
- 并发 connect 后每个被接受的请求都有独立记录；
- 断开一个会话不影响另一个；
- route conflict、full tunnel、失败启动和失败但 native worker 仍活动时，行为保持现有语义；
- Qt 现有会话模型和 QML 测试继续证明多卡片、事件路由和托盘操作不依赖平台。

### Windows agent 构建测试

- 对补丁生成脚本提供固定的 synthetic agent source fixture，验证所有 required replacement 都命中，缺失片段会失败。
- Windows CMake 配置必须构建生成的 agent source，并在目标版本变化时显式失败。
- Windows Release 构建启用现有断言测试，运行 core CTest 和 Qt CTest。

### Windows 实机冒烟测试

新增 PowerShell 入口，使用 `\\.\pipe\openvpn-ui` 发送与 macOS 冒烟测试相同的 JSON 请求；真实凭据只从用户已保存的 Windows Credential Manager 读取，不写入日志或测试文件。测试检查：

- service、daemon 和 Wintun 驱动已就绪；
- 两个 profile 同时连接并保持连接；
- `Get-NetAdapter` 能观察到独立接口，`Get-NetRoute` 和 `Get-DnsClientServerAddress` 与两条配置匹配；
- 断开第一条后第二条仍为 connected 且计时/流量继续增长；
- 断开全部连接后没有残余会话、路由、DNS 和 Wintun/TAP 接口；
- service/daemon 重启后 watchdog 清理所有旧资源。

测试默认拒绝在已有活动会话时运行，并在退出路径清理测试会话；除非显式传入恢复选项，不修改用户原有连接。

## 文档与交付

- 更新 `README.md`、`platform/windows/README.md`、`packaging/windows/README.md`、`docs/windows-acceptance.md` 和 `docs/desktop-acceptance.md`，删除 Windows 单连接预览限制，改为多连接构建和实机验收说明。
- 保留协议 v1 的 `DESKTOP_MULTI` 语义，不修改 macOS 和移动端能力定义。
- 交付前必须分别报告：本机可执行的跨平台测试结果、Windows 构建结果，以及无法在当前环境完成的 Windows 实机项目，并明确不能用 macOS 结果替代实机验收。

## 非目标

- 不修改 macOS helper、macOS agent 补丁或 macOS Qt 交互。
- 不改变协议字段、profile 存储格式和凭据存储格式。
- 不在 Windows 上允许两个全隧道连接同时接管默认路由。
- 不引入多 daemon、多 service 实例或新的 UI 页面。

