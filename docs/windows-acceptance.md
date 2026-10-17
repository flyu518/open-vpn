# Windows 桌面版验收说明

本文说明 Windows 预览版的构建、启动和单连接验收边界。

## 当前能力边界

Windows daemon 在创建时把活动会话上限设置为 `1`。原因是当前接入的 OpenVPN3 Windows agent 只管理一个 TUN；如果允许第二条连接，它可能复用或覆盖第一条连接的虚拟网卡、路由和 DNS。

因此当前 Windows 验收的预期结果是：

- 第一条 VPN 可以建立并保持连接；
- 第二条连接会在启动前返回“当前 Windows 版本暂不支持同时连接多条 VPN”；
- 第一条连接不会被第二条请求中断或替换；
- 断开第一条后，第二条可以正常建立。

这是一项保护性限制。Windows 多连接版本需要为每条会话提供独立的 agent/TUN、路由和 DNS 生命周期，目前尚未完成。

## 验收环境

需要在 Windows x64 主机或虚拟机上完成，macOS 交叉编译和离线测试不能替代真实验收：

- Windows 10/11 x64；
- Visual Studio 2022 MSVC C++ 工具链；
- Qt 6.12 MSVC 版本；
- CMake、OpenSSL、asio、fmt、jsoncpp、lz4、xxHash；
- 固定版本的 OpenVPN3 源码；
- Wintun DLL；
- 管理员权限，用于安装和启动 Windows Service。

## 构建

先把 `third_party/openvpn3.lock` 中记录的版本检出到 `third_party/openvpn3`，并准备 Windows 依赖路径。依赖库的头文件和 `.lib` 路径需要传给 CMake，例如：

```powershell
cmake -S core -B build/core-windows -G "Visual Studio 17 2022" -A x64 `
  -DOPENVPN3_SOURCE_DIR="$PWD/third_party/openvpn3" `
  -DXXHASH_ROOT_DIR="C:/deps/xxHash" `
  -DASIO_INCLUDE_DIR="C:/deps/asio/include" `
  -DFMT_INCLUDE_DIR="C:/deps/fmt/include" `
  -DJSONCPP_INCLUDE_DIR="C:/deps/jsoncpp/include" `
  -DLZ4_INCLUDE_DIR="C:/deps/lz4/include" `
  -DOPENSSL_INCLUDE_DIR="C:/deps/openssl/include" `
  -DOPENSSL_ROOT_DIR="C:/deps/openssl" `
  -DLZ4_LIBRARY="C:/deps/lz4/lib/lz4.lib" `
  -DFMT_LIBRARY="C:/deps/fmt/lib/fmt.lib" `
  -DJSONCPP_LIBRARY="C:/deps/jsoncpp/lib/jsoncpp.lib"

cmake --build build/core-windows --config Release --parallel
```

构建 Qt 桌面版：

```powershell
cmake -S desktop/qt -B build/qt-windows `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" `
  -DOPENVPN_APP_VERSION=0.0.2
cmake --build build/qt-windows --config Release --parallel
C:/Qt/6.12.0/msvc2022_64/bin/windeployqt.exe `
  --qmldir desktop/qt/qml build/qt-windows/Release/openvpn_qt.exe
```

## 启动服务和客户端

使用管理员 PowerShell 安装并启动高权限 agent：

```powershell
build/core-windows/Release/openvpn_windows_service.exe install
sc.exe start openvpn
```

再使用普通用户 PowerShell 启动 daemon 和 Qt 应用：

```powershell
Start-Process build/core-windows/Release/openvpn_windows_daemon.exe
Start-Process build/qt-windows/Release/openvpn_qt.exe
```

也可以直接使用 `packaging/windows/build-qt-package.ps1` 生成压缩包或 NSIS 安装器；正式安装器需要同时包含 Qt 应用、daemon、Windows Service 和 Wintun DLL。

## 功能验证

1. 导入配置 A，输入用户名和密码并连接；
2. 检查界面进入“已连接”，并确认通知区域图标存在；
3. 检查虚拟网卡、地址、路由和 DNS：

   ```powershell
   Get-Service openvpn
   Get-NetAdapter | Where-Object { $_.Name -match "Wintun|OpenVPN" }
   Get-NetIPConfiguration
   Get-NetRoute -AddressFamily IPv4
   Get-DnsClientServerAddress
   ```

4. 导入配置 B 并连接，预期得到单连接限制提示；确认 A 仍保持连接；
5. 断开 A，再连接 B，确认 B 可以建立；
6. 断开 B，检查虚拟网卡、路由和 DNS 是否恢复；
7. 查看 `openvpn_windows_service.exe` 所在目录的 `agent.log`，确认没有崩溃或残留会话。

验收过程中应分别测试认证失败、服务停止、daemon 重启、网络断开、连接取消和路由冲突。真实多连接验收在 Windows 多 TUN 版本完成前不应标记为通过。
