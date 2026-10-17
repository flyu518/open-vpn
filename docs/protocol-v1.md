# VPN 协议 v1

本文档描述桌面界面、会话管理器、OpenVPN3 引擎和平台适配层之间的共享契约。机器可读摘要位于 `schemas/protocol-v1.json`。

## 能力

| 能力 | 活动会话数 | 当前目标平台 |
| --- | ---: | --- |
| `DESKTOP_MULTI` | 大于 1 | Windows、macOS |
| `MOBILE_SINGLE` | 1 | 预留给 Android、iOS |

桌面客户端默认使用分流模式；第二条全隧道连接必须被拒绝；默认情况下互相重叠的路由也必须被拒绝。

平台适配层统一提供 `prepare`、`establish`、`applyRoutes`、`applyDns` 和
`destroy` 阶段。Windows/macOS 当前返回桌面多连接能力；Android/iOS
适配器目前只返回移动端单连接能力和“适配器预留中”诊断。

## 对外 C ABI

`core/include/vpn_engine.h` 只暴露不透明句柄和固定宽度的 C 类型，不允许 C++ OpenVPN3 类型穿过这层边界。

- `vpn_engine_create`：创建指定平台能力的引擎句柄
- `vpn_engine_destroy`：释放引擎句柄
- `vpn_engine_capability`：读取当前能力
- `vpn_engine_max_active_sessions`：读取能力允许的最大活动会话数

该 C ABI 用于共享能力探测和平台边界；桌面实际连接由 OpenVPN3 会话引擎和平台适配层负责。

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

桌面界面的连接顺序使用 `priority` 表示：列表第一项为 `1`，数字越小越优先；拖动卡片会同步更新该值。
当前 v1 的核心冲突校验仍以活动会话和冲突模式为准，`priority` 先作为界面排序和策略元数据保存，
不会绕过严格模式的冲突拒绝。

IPv4 和 IPv6 的网段包含关系都算作冲突；被拒绝的候选策略不能改变已有活动会话。

## 向前兼容

读取方必须忽略未知字段；新增必填字段时必须升级协议版本。密码、私钥和挑战响应不能出现在普通会话快照或日志中。

## macOS 本地 daemon

macOS 桌面界面通过用户权限 Unix socket 与 `openvpn_daemon` 通信，默认路径为
`/tmp/openvpn-ui.sock`。协议采用一行一个 JSON 对象：

- `{"command":"ping"}`：检查 daemon 是否可用。
- `{"command":"snapshot"}`：读取全部会话快照。
- `{"command":"connect","sessionId":"...","profileId":"...","name":"...","profile":"...","credentials":{...},"rememberCredentials":false}`：提交连接请求。省略凭据时由 daemon 从 Keychain 读取。
- `{"command":"disconnect","sessionId":"..."}`：断开指定会话。
- `{"command":"forgetCredentials","profileId":"..."}`：删除指定 profile 的 Keychain 凭据。

连接请求可以附带 `routeStrategy`：

```json
{
  "conflictMode": "compatible",
  "priority": 10,
  "rules": [
    {"type": "cidr", "pattern": "10.20.0.0/16", "action": "prefer", "enabled": true},
    {"type": "ip", "pattern": "192.168.10.4", "action": "exclude", "enabled": true},
    {"type": "domainSuffix", "pattern": "*.corp.example.com", "enabled": true}
  ]
}
```

`compatible` 默认保留先连接会话的路由所有权，并跳过后连接会话的冲突路由；
`strict` 遇到冲突直接拒绝；`ask` 由界面提示用户断开或重试。当前 helper 已执行
冲突模式；`ip`/`cidr` 的 `exclude` 会在当前会话安装路由前过滤匹配网段，`prefer`
会要求用户断开冲突连接后重新连接当前会话。域名和域名后缀规则会加入当前 VPN 的 macOS
分流 DNS 范围；当前 VPN 未提供 DNS 服务器时，域名规则不会生效，实际 IP 路由仍以服务器
下发的 IP/CIDR 为准。

daemon 只在内存中接收凭据，响应和事件经过脱敏；socket 默认权限为当前用户可读写。
真正的 profile 密码应优先由 Keychain/Credential Manager 读取，不能由界面长期保存。
