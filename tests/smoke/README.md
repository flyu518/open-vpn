# 桌面双 VPN 冒烟测试

该测试需要用户提供两个可用的 `.ovpn` 配置，并明确允许建立真实 VPN 连接。

两端的共同验收流程见 [RELEASING.md](../../RELEASING.md)，版本和连接超时以
[project-config.json](../../project-config.json) 为准；以下测试不能代替最终安装包验收。

## macOS 本地测试入口

macOS 构建完成后，可以使用 `openvpn_smoke_cli`。它会先解析配置，随后在终端
隐藏输入用户名和密码，不会把凭据写入配置文件、日志或 Git：

```bash
/tmp/openvpn-release/core/openvpn_smoke_cli \
  /绝对路径/profile-a.ovpn \
  /绝对路径/profile-b.ovpn
```

只做配置解析时，在配置路径前加入 `--offline`；离线模式不会询问密码，也不会建立网络连接。

当前入口只负责会话和事件冒烟；真正双隧道路由验证必须使用已经安装并完成鉴权的
多会话 macOS helper。helper 未安装时不要输入真实密码。

## Windows 本地测试入口

Windows 使用直接链接生产核心的 `openvpn_windows_connection_probe`，先验证双隧道、路由/DNS
隔离及异常清理，再测试最终 EXE，步骤见 [Windows 核心验证与验收](../../docs/windows-acceptance.md)。
本地证书服务端可覆盖握手和失败路径，但不能代替真实服务器认证和业务目标验证。

## 两端共同检查

验证步骤：

1. 导入两个网段不重叠的配置。
2. 同时连接两个 profile。
3. 检查两个会话分别进入“已连接”。
4. 检查两组公司网段分别经过对应的 utun/Wintun 接口。
5. 断开其中一个，确认另一个仍保持连接。
6. 检查路由、DNS 和 helper 状态恢复。

没有用户授权和真实配置时，不运行此测试。
