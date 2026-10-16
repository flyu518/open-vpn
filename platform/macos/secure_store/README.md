# macOS 安全存储

`KeychainCredentialStore` 使用 macOS Keychain 的 generic password 项保存 VPN 用户名、
密码、挑战响应和私钥密码；普通配置文件只保存 profile 元数据，不保存秘密。

凭据字段使用长度前缀二进制编码，不在日志中输出。Keychain 项使用
`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`，删除 profile 时应同步删除对应项。
