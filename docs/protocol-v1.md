# VPN 协议 v1

本文档描述桌面界面、会话管理器、OpenVPN3 引擎和平台适配层之间的共享契约。机器可读摘要位于 `schemas/protocol-v1.json`。

## 能力

| 能力 | 活动会话数 | 当前目标平台 |
| --- | ---: | --- |
| `DESKTOP_MULTI` | 大于 1 | Windows、macOS |
| `MOBILE_SINGLE` | 1 | 预留给 Android、iOS |

第一版桌面客户端默认使用分流模式；第二条全隧道连接必须被拒绝；默认情况下互相重叠的路由也必须被拒绝。

## 对外 C ABI

`core/include/vpn_engine.h` 只暴露不透明句柄和固定宽度的 C 类型，不允许 C++ OpenVPN3 类型穿过这层边界。

- `vpn_engine_create`：创建指定平台能力的引擎句柄
- `vpn_engine_destroy`：释放引擎句柄
- `vpn_engine_capability`：读取当前能力
- `vpn_engine_max_active_sessions`：读取能力允许的最大活动会话数

基础实现只是确定性的引擎句柄。后续任务会在相同契约后面接入 OpenVPN3 客户端。

## 会话状态

会话状态按以下顺序流转：`idle` → `connecting` → `connected` →
`disconnecting` → `disconnected`。连接失败时进入 `failed`。每个状态变化都带有会话标识，界面只能更新对应的会话卡片。

## 错误码

- `VPN_ERR_CAPABILITY_SINGLE_ACTIVE_VPN`（`1001`）：移动端已有活动系统 VPN。
- `VPN_ERR_ROUTE_CONFLICT`（`1002`）：候选路由与活动路由重叠。
- `VPN_ERR_FULL_TUNNEL_EXCLUSIVE`（`1003`）：请求建立第二条默认路由连接。
- `INVALID_ROUTE`（内部决策码）：路由不是合法的 IPv4/IPv6 CIDR。
- `VPN_ERR_DUPLICATE_SESSION`（`1005`）：会话标识已经存在。

## 路由策略

每个会话携带一个 `RoutePolicy`：

- `Split`：只添加配置中明确允许的网段。
- `Full`：接管默认路由，并且与其他活动会话互斥。

IPv4 和 IPv6 的网段包含关系都算作冲突；被拒绝的候选策略不能改变已有活动会话。

## 向前兼容

读取方必须忽略未知字段；新增必填字段时必须升级协议版本。密码、私钥和挑战响应不能出现在普通会话快照或日志中。
