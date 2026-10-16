# 桌面双 VPN 冒烟测试

该测试需要用户提供两个可用的 `.ovpn` 配置，并明确允许建立真实 VPN 连接。

## 本地测试入口

macOS 本地开发构建完成后，可以使用 `openvpn_smoke_cli`。它会先解析配置，随后在终端
隐藏输入用户名和密码，不会把凭据写入配置文件、日志或 Git：

```bash
/tmp/openvpn-core-build-x86/openvpn_smoke_cli \
  /private/tmp/openvpn-profiles/profile-a.ovpn \
  /private/tmp/openvpn-profiles/profile-b.ovpn
```

当前入口只负责会话和事件冒烟；真正双隧道路由验证必须使用已经安装并完成鉴权的
多会话 macOS helper。helper 未安装时不要输入真实密码。

验证步骤：

1. 导入两个网段不重叠的配置。
2. 同时连接两个 profile。
3. 检查两个会话分别进入“已连接”。
4. 检查两组公司网段分别经过对应的 utun/Wintun 接口。
5. 断开其中一个，确认另一个仍保持连接。
6. 检查路由、DNS 和 helper 状态恢复。

没有用户授权和真实配置时，不运行此测试。
