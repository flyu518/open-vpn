# OpenVPN 桌面常驻与更新 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 OpenVPN macOS/Windows 桌面应用增加系统状态栏/托盘、窗口常驻、开机自启、关于和 GitLab Release 更新检查。

**Architecture:** Flutter UI 继续持有唯一的 `SessionViewModel`；`TrayController` 只把现有状态投影到原生托盘菜单并转发动作。`StartupService` 和 `UpdateService` 分别封装系统自启与 GitLab 元数据/安装包启动，所有平台差异留在服务层。

**Tech Stack:** Flutter/Dart 3.13、`tray_manager`、`window_manager`、Dart `dart:io`、macOS LaunchAgent、Windows HKCU Run、GitLab Releases API。

**Spec:** `docs/superpowers/specs/2026-10-17-openvpn-desktop-resident-update-design.md`

## Global Constraints

- 所有 UI、日志、文档和错误提示使用中文。
- 仅实现 macOS 和 Windows；不阻塞未来 Android/iOS 扩展。
- 不保存 VPN 密码、GitLab token 或完整凭据日志。
- 更新必须由用户确认后下载/启动安装器，不做静默替换。
- 不修改用户现有 `.gitignore` 改动。
- 每个任务先写失败测试，再实现，再运行对应测试和全量 Flutter 测试。

## Review Focus

- GitLab 地址未配置、HTTP 非 2xx、JSON 缺字段时，更新检查必须给出可读错误而不是崩溃；由任务 1 测试。
- `v0.1.10`、`0.1.9`、带前缀 tag 和非语义 tag 的比较必须稳定；由任务 1 测试。
- macOS/Windows 自启路径包含空格和中文时不能通过 shell 拼接；由任务 2 测试。
- 托盘初始化失败或非桌面平台运行时，主窗口仍能启动；由任务 3 测试/静态检查。
- 连接数量和流量变化后托盘菜单不能显示旧状态；由任务 3 集成检查。

---

### Task 1: GitLab 更新领域模型与服务

**Files:**
- Create: `ui/lib/services/update_service.dart`
- Create: `ui/test/update_service_test.dart`
- Modify: `ui/pubspec.yaml`（版本保持与发布流程一致）

**Interfaces:**
- Produces `GitLabRelease`, `GitLabReleaseAsset`, `UpdateCheckResult`, `UpdateService.checkForUpdates()`。
- `UpdateService` 接受可替换的 `HttpClient`/下载器边界，纯解析与版本比较可以脱离网络测试。

- [ ] **Step 1: 写失败测试**

  覆盖版本比较、latest release JSON 解析、平台安装包选择、未配置地址和非 2xx 错误。

- [ ] **Step 2: 运行测试确认失败**

  运行 `cd ui && ../.tools/flutter/bin/flutter test test/update_service_test.dart`，预期因服务和模型不存在而失败。

- [ ] **Step 3: 实现最小服务**

  解析 GitLab `tag_name/name/description/assets.links`，通过 `--dart-define` 提供 API/project 配置；版本只接受三段数字，下载器使用临时文件并在用户确认后启动平台安装器。

- [ ] **Step 4: 运行测试确认通过**

  运行同一测试文件，预期全部通过。

- [ ] **Step 5: 提交**

  `git add ui/lib/services/update_service.dart ui/test/update_service_test.dart ui/pubspec.yaml && git commit -m "feat: add GitLab update service"`

### Task 2: 原生开机自启与设置/关于对话框

**Files:**
- Create: `ui/lib/services/startup_service.dart`
- Create: `ui/lib/screens/about_dialog.dart`
- Create: `ui/lib/screens/desktop_settings_dialog.dart`
- Create: `ui/test/startup_service_test.dart`
- Modify: `ui/pubspec.yaml`

**Interfaces:**
- Produces `StartupService.isEnabled/enable/disable` 和 `AboutDialog`/设置界面。
- 消费任务 1 的 `UpdateService` 和版本信息，但不依赖托盘实现。

- [ ] **Step 1: 写失败测试**

  测试 macOS plist 和 Windows Run 值的构造结果，特别是含空格路径；测试未支持平台不抛出未捕获异常。

- [ ] **Step 2: 运行测试确认失败**

  `cd ui && ../.tools/flutter/bin/flutter test test/startup_service_test.dart`，预期失败。

- [ ] **Step 3: 实现平台服务和对话框**

  macOS 使用用户 LaunchAgent，Windows 使用 HKCU Run；所有外部进程使用参数数组。设置界面只保留“开机自启”和“检查更新”，关于界面展示版本和更新入口。

- [ ] **Step 4: 运行测试确认通过**

  运行自启测试和已有 Flutter 测试。

- [ ] **Step 5: 提交**

  `git add ui/lib/services/startup_service.dart ui/lib/screens/about_dialog.dart ui/lib/screens/desktop_settings_dialog.dart ui/test/startup_service_test.dart ui/pubspec.yaml && git commit -m "feat: add desktop startup settings"`

### Task 3: 状态栏/托盘和窗口生命周期

**Files:**
- Create: `ui/lib/services/tray_controller.dart`
- Create: `ui/assets/openvpn_tray.png`
- Modify: `ui/lib/main.dart`
- Modify: `ui/pubspec.yaml`
- Modify: `ui/lib/view_models/session_view_model.dart`（必要时补充托盘刷新通知）

**Interfaces:**
- `TrayController.initialize`、`refresh`、`dispose`；接收打开、连接、断开、更新、设置和退出回调。
- 消费现有 `VpnSession`/`SessionViewModel`，不复制连接状态。

- [ ] **Step 1: 写失败测试/静态契约**

  增加托盘菜单投影的纯函数测试：服务状态、活动连接数、每条 session 的连接动作和中文标签；测试非 macOS/Windows 时 no-op。

- [ ] **Step 2: 运行测试确认失败**

  `cd ui && ../.tools/flutter/bin/flutter test test/tray_controller_test.dart`，预期失败。

- [ ] **Step 3: 实现托盘和窗口**

  接入 `tray_manager` 与 `window_manager`；窗口关闭隐藏、菜单打开窗口、连接/断开调用现有回调；监听 `SessionViewModel` 后刷新菜单；托盘失败不能阻止主界面。

- [ ] **Step 4: 运行测试确认通过**

  运行托盘测试、全量 Flutter 测试和 `flutter analyze`。

- [ ] **Step 5: 提交**

  `git add ui/lib/services/tray_controller.dart ui/lib/main.dart ui/assets/openvpn_tray.png ui/pubspec.yaml ui/test/tray_controller_test.dart && git commit -m "feat: add desktop tray controls"`

### Task 4: 发布配置、中文文档和桌面验证

**Files:**
- Modify: `README.md`
- Modify: `ui/README.md`
- Modify: `packaging/macos/README.md`
- Modify: `packaging/windows/README.md`
- Modify: `ui/pubspec.yaml`（版本/构建说明）

- [ ] **Step 1: 写发布检查**

  在文档中固定 GitLab `--dart-define`、macOS/Windows 构建命令、更新资产命名和安装后的托盘行为。

- [ ] **Step 2: 运行构建验证**

  `cd ui && ../.tools/flutter/bin/flutter test`；`../.tools/flutter/bin/flutter analyze`；macOS 执行 release build；根目录运行 C++ ctest。

- [ ] **Step 3: 检查实际产物**

  确认 macOS app 不含沙盒限制、托盘图标资产进入 bundle、版本号和 GitLab tag 说明一致；Windows 至少完成 `flutter build windows`（若当前机器无 Windows 工具链则明确记录）。

- [ ] **Step 4: 提交**

  `git add README.md ui/README.md packaging/macos/README.md packaging/windows/README.md ui/pubspec.yaml && git commit -m "docs: document desktop tray and updates"`

