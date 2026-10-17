# Windows 桌面版验收说明

本文说明 Windows 多会话版本的构建、启动和双配置验收方式。Windows 与 macOS 使用同一套连接列表、会话生命周期和路由冲突策略；Windows 的原生 agent 负责为每个会话隔离 TUN、事件、进程句柄、路由和 DNS 状态。

## 当前能力边界

Windows daemon 的活动会话上限为 `0`，表示不限数量。每条连接由配置会话 ID 隔离；Windows agent 对 `tun-open` 与 `tun-setup` 的跨命名管道生命周期按 PID 和网卡 GUID/索引重新关联，避免第二条连接复用或覆盖第一条连接的虚拟网卡、路由和 DNS。

因此 Windows 的预期结果与 macOS 一致：

- 配置 A 和配置 B 可以同时建立并保持连接；
- 两条连接拥有独立的虚拟网卡、计时、流量、路由和 DNS 生命周期；
- 断开 B 不会断开 A，断开 A 不会断开 B；
- 断开最后一条连接后，虚拟网卡、路由、DNS 和 agent 句柄都应清理；
- 严格/兼容路由冲突策略与 macOS 使用相同的会话规则。

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

也可以只构建 Windows 原生链路：

```powershell
cmake --build build/core-windows --config Release `
  --target openvpn_windows_service openvpn_windows_daemon --parallel
```

构建 Qt 桌面版：

```powershell
cmake -S desktop/qt -B build/qt-windows `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" `
  -DOPENVPN_APP_VERSION=0.0.3
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

## 自动化验证

在使用 vcpkg 动态库时，CTest 需要能找到 jsoncpp 等运行库：

```powershell
$v = (Resolve-Path "third_party/openvpn3/deps/vcpkg_manifests/windows/vcpkg_installed/x64-windows").Path
$env:PATH = "$v/bin;$env:PATH"
ctest --test-dir build/core-windows -C Release --output-on-failure
```

其中 `openvpn_windows_agent_patch_tests` 会用固定版本的 OpenVPN3 Windows agent 生成真实源码并检查关键边界；`openvpn_daemon_session_limit_tests` 会确认 Windows 桌面会话不限数量且不同会话互相隔离。

## 双配置功能验证

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

4. 在本项目 Qt UI 的同一个连接列表中导入配置 B 并连接，预期 A/B 同时显示“已连接”；
5. 确认系统中至少有两条活动的 Wintun、TAP 或 OpenVPN DCO 虚拟网卡，并分别检查两条连接的地址、路由和 DNS；
6. 只断开 B，确认 A 仍保持连接且 A 的流量计时继续；
7. 断开 A，确认所有 VPN 虚拟网卡、专属路由和 DNS 配置恢复；
8. 查看 `openvpn_windows_service.exe` 所在目录的 `agent.log`，确认没有崩溃或残留会话。

仓库提供了交互式验收脚本，脚本不会读取密码或替代 UI 操作，只负责检查 Service、虚拟网卡数量和断开后的清理：

```powershell
.\platform\windows\tests\windows_multisession_smoke.ps1 `
  -ProfileA C:\vpn\profile-a.ovpn `
  -ProfileB C:\vpn\profile-b.ovpn `
  -Daemon .\build\core-windows\Release\openvpn_windows_daemon.exe `
  -QtApp .\build\qt-windows\Release\openvpn_qt.exe `
  -LaunchApplications
```

真实 Windows 主机仍需使用可连接的两份配置、Wintun/TAP 驱动和管理员权限完成上述脚本；本地 MSVC 编译和离线测试不能替代 VPN 服务端、驱动和 DNS 的实机验证。

验收过程中还应分别测试认证失败、服务停止、daemon 重启、网络断开、连接取消和路由冲突。
