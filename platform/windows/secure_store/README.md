# Windows 安全存储

`WindowsCredentialStore` 使用 Windows Credential Manager 的 generic credential 保存 VPN
用户名、密码、挑战响应和私钥密码；普通配置文件只保存 profile 元数据，不保存秘密。

凭据字段使用长度前缀二进制编码，不在日志中输出。删除 profile 时应同步调用 `remove`，
不能把 Credential Manager 的内容复制到普通配置文件。
