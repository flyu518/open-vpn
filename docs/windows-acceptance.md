# Windows 桌面版验收说明

本文说明 Windows 多会话版本的构建、启动和双配置验收方式。Windows 与 macOS 使用同一套连接列表、会话生命周期和路由冲突策略；Windows 的原生 agent 负责为每个会话隔离 TUN、事件、进程句柄、路由和 DNS 状态。

版本与共享参数以根目录 [project-config.json](../project-config.json) 为准，更新操作遵循
[发布操作约定](../RELEASING.md)。下文的 0.0.13/0.0.14 是已执行的历史实测，不能重标为 1.0.0 验收。

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
- Qt 6.8 或更新版本的 MSVC 版本；
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
  --target openvpn_windows_service openvpn_windows_daemon openvpn_windows_daemon_launcher --parallel
```

构建 Qt 桌面版：

```powershell
cmake -S desktop/qt -B build/qt-windows `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -UOPENVPN_APP_VERSION
cmake --build build/qt-windows --config Release --parallel
C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe `
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
Start-Process build/core-windows/Release/openvpn_windows_daemon_launcher.exe
Start-Process build/qt-windows/Release/openvpn_qt.exe
```

正式安装使用 `packaging/windows/build-qt-package.ps1 -BuildInstaller` 并通过 `-VcRedist` 提供微软签名的 x64 VC++ 运行库，生成包含 Qt 应用、daemon、Windows Service、Wintun 和离线运行库前置依赖的标准 EXE；安装服务需要管理员授权，桌面应用使用普通用户运行。未传 `-BuildInstaller` 的 ZIP 仅供开发，不是安装程序。

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

每个候选都应在真实 Windows 主机使用两份可连接配置和 Wintun/TAP 驱动完成上述验证；本机已有结果见下文。本地 MSVC 编译和离线测试不能替代 VPN 服务端、驱动和 DNS 的实机验证。

验收过程中还应分别测试认证失败、服务停止、daemon 重启、网络断开、连接取消和路由冲突。

## 独立核心技术验证（先于重新打包）

`openvpn_windows_connection_probe` 直接链接生产 `SessionController` 和 OpenVPN3，
不使用 Qt、不使用官方客户端 UI，也不需要反复安装。必须连接本次构建的 Windows agent；
agent 的官方安全检查要求它和 probe 位于同一目录。不要用旧安装目录的服务替代新版 agent。
测试时先关闭本项目客户端，停止本项目已安装服务，运行构建目录中的
`openvpn_windows_service.exe run`（服务端需要管理员权限），probe 本身可用普通用户运行。
结束后停止测试 agent 并恢复原来的服务。不要停止 OpenVPN Connect 等其他产品的服务。

```powershell
cmake --build build/core-windows --config Release --target openvpn_windows_service openvpn_windows_connection_probe
```

准备官方 OpenVPN Community Windows MSI 中的原生服务端程序及其 DLL，放入
`tmp/openvpn-official-server`。`prepare_core_fixture.ps1` 会校验服务端的 OpenVPN Inc 签名，
生成两天有效的本地测试证书、两份标准 `.ovpn` 配置，并在 loopback 的 21194/21195
端口启动两个 TLS 服务端。测试数据只能写入工作区 `tmp`，不会替换用户导入的真实配置。
脚本重复运行会校验 PID/路径后关闭上次的两个测试服务端。

```powershell
./platform/windows/tests/prepare_core_fixture.ps1
./build/core-windows/Release/openvpn_windows_connection_probe.exe --fixture "$PWD/tmp/openvpn-core-fixture/profiles.json"
```

此用例实际创建两张 Wintun 网卡并完成 TLS 握手，检查并发连接、重叠路由保留先连接者、
单条断开/重连、IP/路由不被另一条修改、分流 DNS 与 IPv6 防泄漏路由的生命周期。
`--fixture-strict` 验证重叠路由拒绝第二条且不破坏第一条；`--fixture-failures`
使用本地 TCP 黑洞验证握手取消和共享的 30 秒超时，失败状态不能被随后的断开事件覆盖。
测试必须串行运行，不能同时修改同一台机器上的网络状态。

`--fixture-crash` 在两条连通后故意以退出码 97 强退（跳过析构），随后轮询
`--fixture-check-cleanup`，并检查该次输出的网卡编号已经消失，以验证服务的进程死亡清理。
清理包含 netsh/DNS 操作，不能仅固定等待两秒就判定泄漏。

2026-10-18 本机验证已通过：完整双连接生命周期、分流 DNS/IPv6 隔离、握手取消、
30 秒超时（约 30074ms）、进程强退清理；核心 CTest 16/16、Qt CTest 9/9。
原始运行输出位于本机忽略目录 `tmp/core-probe-*.log`。
这些证据不代表真实远端业务流量或最终安装包已经验收通过：本地服务端使用 `dev null`，
只验证握手、驱动和系统网络配置；真实服务器认证及最终 EXE 仍需分别验收。

额外的 `openvpn_windows_ipv6_probe` 需要管理员权限，必须在测试 agent 停止且没有
活动测试隧道时显式运行。`--crash` 创建带所有权标记的三条路由后以 97 强退，
`--recover` 在新进程中验证回收与再次连接后的释放；`--preserve-existing` 检查其他
所有者的已有路由不被删除，`--live-owner` 检查另一进程无法回收存活 owner 的路由。
这些原生 OS 用例也已通过。服务使用受保护的机器级 owner gate 和 HKLM write-ahead
journal，不能只按固定 IPv6 前缀清理。

真实配置若出现 `AUTH_FAILED`，可使用 `--run-compare <profiles.json> <官方openvpn.exe>`
做认证对照。仅在本项目引擎被拒后，才将同一次输入的凭据通过匿名 stdin 管道交给
官方原生引擎；不使用官方 UI、不落密码文件、不把密码放入命令行，也不输出原始日志。
官方参考进程使用 `dev null`、`ifconfig-noexec`、`route-noexec` 和 `script-security 0`，
只验证认证，不替代产品引擎。应先验证该参考程序的 OpenVPN Inc 签名。
`--fixture-reference` 可用本地证书服务端和 dummy 凭据验证该管道；本机已通过。

随后本机两条已导入真实配置也通过了独立核心全流程：同时连通（不同 Wintun 网卡）、
第二条断开/重连不改变第一条 IP/路由、第一条断开后第二条保持正常、最终退出清理。
此前第二条的 `AUTH_FAILED` 在该次复测及重连中未复现，不能据此推断之前的拒绝原因；
本次没有触发官方引擎认证回退对照。该记录仍不等于安装包或业务端到端流量验收。

## 本机安装版验收记录（2026-10-18）

- 标准 Inno Setup 中文安装包 0.0.13 已实际升级安装；安装日志确认成功、无需重启，
  UI、daemon、launcher、service、Wintun 与本次构建逐一 SHA-256 校验一致。
- 实际运行 `C:\Program Files\OpenVPN\OpenVPN.exe`，普通用户启动，服务可用，
  没有出现此前的 named pipe Access denied 错误。
- 两条真实配置在该安装版同时在线，Wintun 接口分别为 61、63；
  第二条明确提示重叠路由已跳过。断开/重连第二条不改变第一条 IP/路由，
  再断开第一条也不改变第二条 IP/路由。第二条认证成功后，安全存储及免重复输入重连已验证。
- 设置中的自启动实际开启/关闭，并核对 HKCU Run；最后恢复为原来的关闭状态。
  使用临时证书配置实际导入、改名、切换流量图时间范围、添加 IP/CIDR 优先规则和
  域名后缀排除规则、校验无效输入、保存严格模式、导出标准配置并核对内容哈希。
  删除凭据确认框取消正常；临时配置的删除确认及删除后返回列表已观察，原文件保留。
- 真实连接的详情、滚动、诊断展开/收起、活动连接禁止删除已检查。
  退出确认取消正常；实际退出界面后 daemon 和 VPN 保持运行，重开界面恢复活动状态。
- 实际 UI 检查发现两处 Windows 页面错误显示“macOS 分流 DNS”；已修改共享说明为
  “系统分流 DNS”，并新增回归断言，先失败后通过；没有更改 macOS 的操作流程。
- 包含文案修正的 0.0.14 标准 EXE 已实际覆盖升级成功，安装退出码 0，注册版本 0.0.14，
  服务恢复 Running，旧测试网卡清理为 0；上述五项安装文件哈希全部与构建一致。
  已重新打开最终安装目录的 EXE，原有两条配置保留，取消连接后可再次连接。
- 0.0.14 安装版再次实际连接两条真实配置成功，系统两张 Wintun 均为 Up；
  “关于”确认版本 0.0.14，DNS 说明已为“系统分流 DNS”。拖拽排序和恢复原排序、
  最大化/还原、最小化后重新激活、关闭窗口后重新打开均已实际操作；只有一个 UI 进程。
- 手动“检查更新”按钮已实际点击，界面显示网络请求失败。对照发现系统代理
  `127.0.0.1:7890` 请求 GitHub Releases API 返回 403（API rate limit exceeded），
  同时直连返回 200；没有修改用户的代理设置，也没有绕过设置自动下载或安装更新。
- 最终已用 UI 逐条结束测试连接，测试网卡、接口地址、IPv4 路由及带 owner 标记的
  IPv6 防泄漏路由均清理为 0；安装服务仍为 Running。真实配置、原始文件和原排序保留。
  再次 fetch 后执行 `merge --ff-only origin/main` 返回 Already up to date，未丢弃本地修改。

界面逐项验证与自动化测试是不同证据：托盘菜单未被当前电脑控制工具暴露为可定位窗口。
移动主窗口后可以观察菜单截图，但实际输入先激活主窗口、菜单收起，工具随后拒绝点击；
因此不能据自动化菜单测试宣称已逐项人工点击托盘菜单。更新下载安装、卸载及真实业务服务器
可达性也应单独记录，不因握手和流量计数正常而视为完成。

后续用户手动操作托盘菜单并反馈“现在没有问题了”。此项属于用户人工反馈，不应改写为工具已逐项代点通过。

## 统一配置改动验证（2026-10-18）

共享配置将下一候选版本设为 `1.0.0`，连接超时仍为 30 秒，认证失败不无限自动重试。
Windows 生产目标已重新构建，core CTest 17/17、Qt CTest 12/12 通过；共享配置的类型、范围、
版本一致性、生产构建禁止替换配置、打包早期拒绝冲突版本，以及 Windows EXE 版本资源均有回归检查。
Mac 打包脚本仅在本机通过 Bash 语法及冲突版本拒绝检查，未在 Windows 上执行原生 Mac 构建。

本轮没有安装或发布 1.0.0，没有替换正在使用的 0.0.14；两端必须从同一候选提交重新打包、安装和实测后才能发布。
