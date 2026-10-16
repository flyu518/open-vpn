# macOS 打包

发布包需要为桌面应用和高权限 helper 分别签名，并配置 launchd 生命周期。正式发布前还要提供 arm64 或 universal 依赖，不能使用当前仅供本机 x86_64 验证的 Homebrew 库。
