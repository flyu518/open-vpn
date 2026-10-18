# OpenVPN 多平台更新与发布约定

macOS 和 Windows 使用同一套更新顺序、产品版本、验收标准和 Release。平台差异只限于构建工具、安装包格式、系统授权和底层网络适配，不分别维护两套产品版本。

适用范围：Windows x64、macOS Apple Silicon 和 Intel。其他平台正式接入后沿用相同流程，但必须尊重系统 VPN 能力边界，不能直接宣称具有桌面端的多连接能力。

## 统一操作顺序

所有平台都按下面的顺序执行，不能跳过安装包实测：

1. 同步代码，确认工作区和待验收提交。
2. 由发行负责人统一确定版本和发布范围。
3. 修复问题，补充测试，提交源码，冻结候选提交。
4. 各平台从同一候选提交构建应用、后台服务和安装包。
5. 各平台使用安装后的应用完成同一份验收清单。
6. 汇总提交号、测试结果、安装包版本、哈希和已知限制。
7. 所有目标平台通过后，统一创建标签并发布一个 Release。

任一阶段发现问题，回到第 3 步。代码修复会产生新的候选提交；所有平台必须重新构建该提交并完成对应回归，不能沿用另一个提交的验收结果。

## 本次从 Windows 交接到 Mac

当前先同步源码和完成候选验收，不创建或推送 `v1.0.0` 标签：

1. Windows 确认修改已提交、工作区干净，核对远程后只推共享分支。以下以 `main` 为例，显式关闭附带标签推送：

   ```text
   git status --short
   git remote -v
   git -c push.followTags=false push origin main
   git rev-parse HEAD
   ```

2. Mac 按“同步代码与冻结候选提交”一节的命令快进到该提交，确认完整 SHA 与 Windows 一致，执行共享配置检查，再构建 PKG 并实际安装、测试。Intel 和 Apple Silicon 都在发布范围内时，分别完成对应架构的验收。
3. Windows 也从该候选构建 `1.0.0` 标准 EXE，实际安装并测试；已安装的 `0.0.14` 和它的历史记录不能替代这一步。
4. 任一端修复问题，就提交并同步新候选，双方按新提交重新构建和回归。先记录源码 SHA，再将验收结果另行提交；仅追加验收记录的文档提交不改变已测源码，但标签仍须指向双方记录的同一个候选 SHA。
5. 两端及目标架构全部通过后，先确认“标签与 Release”一节的跨平台发布关卡已实现，再由发行负责人创建一个标签、汇总一个草稿 Release，最后正式发布。

不要先打标签再开始第一次打包，也不要 Mac 测过就直接发布、随后再补 Windows 包。需要先验收的对象是最终将要上传的安装包，而不只是构建目录中的程序。

## 产品版本与构建编号

首个统一正式版以 `1.0.0` 为目标；只有当前代码在两端完成回归后才能正式发布。版本统一不等于把旧 Mac 包改名，或将已经测试的 Windows 包直接重标为新版本。

| 变更 | 版本处理 | 平台处理 |
| --- | --- | --- |
| 仅 Windows 修复 Bug | `1.0.0` 升到 `1.0.1` | 两端版本都为 `1.0.1`，Mac 无功能变更也重新构建和验收 |
| 仅 macOS 修复 Bug | `1.0.1` 升到 `1.0.2` | 两端版本都为 `1.0.2`，Windows 同步构建和验收 |
| 共享核心或 UI 修复 | 增加修订号 | 两端重点回归相同行为和资源清理 |
| 向后兼容的新功能 | 例如 `1.1.0` | 两端同步发布，明确各平台支持范围 |
| 配置格式或公开接口不兼容 | 例如 `2.0.0` | 提供迁移说明，验证已有配置和凭据的处理 |

- 不把一次重编译、一次测试或一次安装当作一次正式发版。
- 内部候选用独立的构建编号和 Git 提交号区分，例如“产品 1.0.1，候选 3，提交 abc1234”；候选包放在不同目录，不能相互覆盖导致测试对象不明。
- 已发布安装包的内容有变化就必须发布新产品版本，包括安装器、签名和随包依赖的修复；不得用同一个正式版本替换旧包。
- 未受影响平台可以在发布说明中标注“无功能变更”，其用户不强制升级。
- 当前更新器只识别三段数字版本，不直接使用 `-rc.1` 或 `+build` 作为应用版本；预发布通道需要先实现解析和通道选择。

## 版本来源与当前实现

根目录 [project-config.json](project-config.json) 是产品版本和共享连接策略的唯一来源。发行负责人修改一次并提交，其他平台同步后读取，不在各机器上重复改版本，也不另建 `VERSION` 文件。

| 配置项 | 当前值 | 使用范围 |
| --- | --- | --- |
| `schemaVersion` | `1` | 配置格式版本，不是产品版本 |
| `version` | `1.0.0` | Qt 项目、关于、Mac Bundle、两端安装包和发布工作流 |
| `connectionTimeoutSeconds` | `30` | 两端 OpenVPN3 建立连接的超时秒数，允许 1 到 3600 的整数 |
| `retryOnAuthFailed` | `false` | 两端认证失败后不无限自动重试，返回凭据处理流程 |

两端在仓库根目录执行完全相同的命令即可读取或验证配置，需 CMake 3.21 或更新版本：

```text
cmake -DOPENVPN_CONFIG_KEY=version -P cmake/ProjectConfig.cmake
cmake -DOPENVPN_CONFIG_KEY=connectionTimeoutSeconds -P cmake/ProjectConfig.cmake
cmake -DOPENVPN_CONFIG_KEY=retryOnAuthFailed -P cmake/ProjectConfig.cmake
cmake -P tests/config/test_project_config.cmake
```

CMake 在构建时生成 C++ 常量，共享核心使用生成的值；程序运行时不读取外部 JSON。因此修改配置后必须重新构建、打包和验收，不会改变已经安装或正在运行的旧版。

两端打包脚本默认读取配置。保留的 `-Version`、Mac 版本位置参数和 `OPENVPN_VERSION` 只用于一致性确认，不是第二个版本来源；指定不同版本会在构建或安装操作之前失败。旧 Qt 缓存版本不一致时会明确报错；重新配置时加 `-UOPENVPN_APP_VERSION` 清理该缓存，打包脚本已处理此步骤。

发布工作流也会核对标签或手动输入与配置一致。当前还没有单一命令完成跨平台全部构建和发布，正式发布仍必须执行本文的汇总验收关卡。

## 同步代码与冻结候选提交

在仓库根目录执行以下只读检查，两端相同：

```text
git status --short
git branch --show-current
git remote -v
```

工作区不干净时，先审查并提交或妥善保存自己的修改；不得自动丢弃、强制重置或覆盖另一平台的工作。确认工作区干净后，再同步双方约定的共享分支，例如 `main`：

```text
git fetch origin
git switch main
git merge --ff-only origin/main
git rev-parse HEAD
git status --short
```

存在分叉就停止快进操作，先审查并解决；不要为通过检查而强推。每台机器记录完整的候选提交 SHA；仅分支名称相同不代表源码相同。

需要带到另一台机器的源码、测试和文档必须先提交，并在确认远程和范围后推送共享分支。只提交本约定文档不会把本机尚未提交的 Windows 修复带到 Mac。验收中的本地代码不能被当作远程候选提交。

## 构建与自动化测试

共同要求：锁定 `third_party/openvpn3.lock` 指定的源码提交，记录 Qt、编译器、依赖版本及目标架构。正式候选使用新的构建目录；不得混入旧版本的 daemon、helper、service、驱动或动态库。所有命令都从仓库根目录执行，任一命令失败就停止，不能继续使用残留产物。

下面以候选版本 `1.0.0` 为例，并不执行发布。工具路径需替换为本机实际位置。命令不同不改变共同流程或验收要求。

### Windows 构建命令

先按本机 MSVC 和依赖环境配置一个新的 core 构建目录，并确认它使用真实 OpenVPN3、官方签名 Wintun 及完整运行时。具体依赖说明见 [Windows 打包说明](packaging/windows/README.md)。以下假设配置后的目录为 `build/core-release-windows`。

```powershell
$releaseVersion = cmake -DOPENVPN_CONFIG_KEY=version -P cmake/ProjectConfig.cmake
$releaseCoreDir = 'build/core-release-windows'
$releaseQtDir = 'build/qt-release-windows'

cmake --build $releaseCoreDir --config Release
ctest --test-dir $releaseCoreDir -C Release --output-on-failure

./packaging/windows/build-qt-package.ps1 `
  -Version $releaseVersion `
  -BuildDir $releaseQtDir `
  -QtDir 'C:/Qt/6.8.3/msvc2022_64' `
  -BuildInstaller `
  -VcRedist 'C:/deps/VC_redist.x64.exe' `
  -Output "dist/OpenVPN-$releaseVersion-windows-x64.exe" `
  -WindowsDaemon "$releaseCoreDir/Release/openvpn_windows_daemon.exe" `
  -WindowsDaemonLauncher "$releaseCoreDir/Release/openvpn_windows_daemon_launcher.exe" `
  -WindowsService "$releaseCoreDir/Release/openvpn_windows_service.exe" `
  -WintunDll "$releaseCoreDir/Release/wintun.dll" `
  -RuntimeDllDir "$releaseCoreDir/Release"

cmake --build $releaseQtDir --config Release
ctest --test-dir $releaseQtDir -C Release --output-on-failure
```

PowerShell 中原生命令失败不一定自动终止会话，每条命令都必须检查退出码为 0；脚本编排时应显式检查 `$LASTEXITCODE`。打包脚本只负责构建桌面目标，不能把“打包成功”当作 Qt 测试已通过。

只交付标准安装 EXE；便携 ZIP 和测试 Wintun 替身不得作为正式安装包。构建命令不要求用管理员身份运行桌面应用，安装服务时的 Windows UAC 由操作者确认。

### macOS 构建命令

在对应原生架构机器上准备 Qt 和依赖，不能把 Windows 构建目录或另一架构的 Homebrew 库复制过来使用。具体环境说明见 [macOS 打包说明](packaging/macos/README.md)。

```bash
release_version=$(cmake -DOPENVPN_CONFIG_KEY=version -P cmake/ProjectConfig.cmake)
release_arch=$(uname -m)
release_brew_prefix=$(brew --prefix)
release_build_dir=$(mktemp -d /tmp/openvpn-release.XXXXXX)
release_qt_root=/替换为本机Qt目录/macos

./packaging/macos/build-release.sh \
  "$release_qt_root" \
  "$release_build_dir" \
  "$release_version" \
  "$(brew --prefix xxhash)" \
  "$release_arch" \
  "$release_brew_prefix"
```

该脚本构建并测试 core 与 Qt，再打包 app、daemon 和 helper；退出码必须为 0。Intel 使用 `x86_64` 和对应 Intel 依赖，Apple Silicon 使用 `arm64` 和对应 ARM 依赖。一个架构通过不能代替另一个架构的验收。

本地安装生成的 `OpenVPN-1.0.0-macos-<架构>.pkg`，然后打开 `/Applications/OpenVPN.app`。不能只运行构建目录的应用就认为 PKG 通过。正式对外发布还必须验证 Developer ID 签名、公证、安装权限和最低系统版本；尚未满足时只能作为明确标注限制的内部候选分发。

## 安装包实机验收

每个平台都必须安装本次候选包，并在“关于”核对版本。记录实际启动路径，确认运行的不是旧版或构建目录程序。升级安装应关闭本产品旧进程、更新服务并保留配置，不影响其他官方 OpenVPN 产品。

- [ ] 首次安装和从上一正式版本升级；后台服务可用、没有意外命令行窗口、重复实例或旧进程占用。
- [ ] 导入和取消导入、重复或无效配置提示、重命名、导出、拖拽排序；测试数据与真实配置分开。
- [ ] 设置、自启动开关、关于、检查更新、提示关闭，以及所有确认框的确认和取消；失败原因可读，按钮没有被提示遮挡。
- [ ] 状态栏或托盘的打开、连接、取消、断开、刷新、设置、关于、退出；没有整栏反复刷新、闪烁或点击失效。
- [ ] 两条真实配置同时连接，不只看 UI 状态，还检查独立网卡、地址、路由、DNS，以及双方约定的业务目标可达性。
- [ ] 单条断开和重连不改变另一条连接的地址、路由或状态；凭据保存后可重新读取，取消或认证失败可以恢复。
- [ ] 兼容模式保留先连接者的重叠路由，严格模式拒绝冲突；修改策略按既有逻辑重连后生效。
- [ ] 错误密码、不可达服务端、取消、共享连接超时、daemon 或 helper/service 异常均有明确结果，不无限停在连接中。
- [ ] 详情、流量、诊断、滚动、不同窗口尺寸、关闭窗口与退出界面的行为和原有 macOS 约定一致。
- [ ] 最终断开后清理本次网卡、专属路由、DNS 和共享防泄漏状态；原有网络、配置顺序与用户设置恢复。

Mock 和本地测试服务端用于回归失败路径，不能代替真实双连接和业务数据面的验收。密码只在本机输入，不进入聊天、参数、文件或日志。破坏性测试只操作明确的测试目标；会影响用户网络或数据时先协调测试窗口。

共享核心和 Qt UI 在两端保持同一套业务逻辑，不为修复 Windows 问题擅自改变 macOS 交互。平台差异放在适配层。自动化、人工操作、用户代操作和未测项目分别记录，不把一端或一种测试的通过当作全部通过。

## 汇总验收记录

各平台填写相同字段，统一保存在该次发布的记录中，例如 `docs/releases/1.0.0.md`。不要把密码、真实私钥、原始配置或未脱敏日志提交到仓库。

```text
产品版本：
候选提交完整 SHA：
平台、系统版本与架构：
构建编号与工具版本：
安装包文件名、大小与 SHA-256：
应用实际启动路径与关于版本：
core 和 Qt 测试命令、退出码、通过数量：
安装、升级、真实双连接与业务目标验证结果：
各 UI 和状态栏或托盘项目的操作结果：
断开和异常退出后的资源清理结果：
操作者与日期：
已知限制、失败和未测项：
验收结论：通过或阻止发布
```

记录必须绑定实际安装的包；包重新生成后应重新校验和安装复测，不能沿用旧包哈希。Mac 和 Windows 记录的候选 SHA 不一致时，不能进入正式发布。

如果 CI 根据标签重新构建，即使源码 SHA 和版本完全相同，也不能假定二进制与本机已测包一致。应让 CI 复用已验收且哈希一致的候选资产；否则必须下载 CI 新产物，重新安装验收并记录哈希，再允许发布。仅追加验收文档的提交与用于构建的候选源码 SHA 应分别记录，不能因补交记录而把标签悄悄改指向未经双方确认的提交。

## 标签与 Release

统一发行资产命名：

```text
OpenVPN-1.0.0-windows-x64.exe
OpenVPN-1.0.0-macos-arm64.pkg
OpenVPN-1.0.0-macos-x86_64.pkg
SHA256SUMS
```

`SHA256SUMS` 包含全部安装包的 SHA-256。应用更新器还依赖 GitHub Release 资产的 `digest`（`sha256:...`）和大小校验，只有额外的校验文本文件并不能代替客户端需要的资产元数据。

发布说明使用同一个版本标题，分别列出“共享改动”“Windows”“macOS”“升级与兼容性”“已知限制”。只有 Windows 修复时，Mac 写明“无功能变更，同步版本”，而不是创建 Windows 专属版本标签。

正式发布流程只有发行负责人执行一次：

1. 确认所有目标平台、架构、签名和验收记录已经通过，并冻结候选提交。
2. 确认发布工作流已经具备下述跨平台汇总关卡。
3. 创建 `v1.0.0` 形式的唯一版本标签，指向验收的候选提交；同名标签已存在就停止，不移动或强推标签。
4. 将所有安装包和校验文件汇总到同一个草稿 Release，核对标签、版本、大小、哈希、签名及资产 `digest`。
5. 全部检查通过后发布，并验证两端“检查更新”均能匹配本平台和架构的包。

**当前自动化限制：** `.github/workflows/release-macos.yml` 在推送匹配 `v*.*.*` 的标签或手动运行时，会构建 Mac 并直接创建 Release；它没有等待 Windows 安装包和验收。跨平台汇总关卡完成前，不得按本约定直接推送正式标签或手动触发该工作流来发布统一版本。

统一工作流需要先做到：所有平台构建任务只上传候选资产，由唯一发布任务等待全部任务成功、验收确认后汇总草稿，再正式发布；任一平台失败均不得只发布其余平台。构建后的资产必须就是安装验收的资产，不能验收后又无记录地重编译。

当前客户端只读取全局最新 Release。默认不单独正式发布某个平台，否则另一平台可能收到更新提示却没有安装包。紧急单平台修复若不能等待其他平台，先作为内部候选交付；如需长期独立发布，必须另行设计按平台选择最新可用版本的更新协议。

发现已发布版本有问题时，明确标注受影响版本和处理方式，修复后发布下一个版本；不复用旧标签，也不静默替换旧安装包。

## 旧版本内容与清理边界

判断残留时，以生产构建是否仍引用它为准，不按搜索到旧版本号就全部替换或删除：

- `docs/desktop-acceptance.md`、`docs/windows-acceptance.md` 的旧版本号对应历史实测，保留原版本及结论，不能改成当前候选。
- 更新检查测试中的旧版本号用于验证版本比较，不是产品版本来源；QML 模块版本、协议版本、配置格式版本也不随产品版本统一修改。
- Windows 安装器删除旧 `install-iexpress.cmd`、打包脚本排除旧脚本和测试产物的逻辑属于升级兼容保护，必须保留。当前正式路径为 Inno Setup EXE，不再生成旧自解压安装器。
- `platform/*/adapter_contract.h` 的桌面占位函数用于能力契约测试，`platform/*/adapter/` 是离线路由状态模型；它们不是生产 OpenVPN3 agent 的连接入口。不要把占位诊断当作当前桌面端未实现，也不要把离线模型通过当作原生隧道通过。
- 本机 `build/`、`dist/`、`tmp/` 中的旧包、日志和试验数据均不随 Git 交接。正式交付只选择验收记录中的准确文件和哈希，不使用整个 `dist` 目录通配上传，也不把 ZIP 当安装器上传。
- 清理本机旧产物前，核对是否仍被测试、回退或验收记录使用；真实配置、凭据和原始日志不能作为“旧文件”自动删除。需要清理时先明确目录和保留范围，优先归档而非不可恢复删除。

## 提交与跨机器交接

- 提交前运行 `git diff --check`，审查源码、测试、版本和文档的范围；仅暂存本次需要提交的文件，不使用 `git add .` 混入无关修改。
- 提交说明写清功能或修复及平台范围，保存验证命令和结果；文档提交不能冒充源码已提交、安装包已发布。
- 提交与推送分开报告；本地提交成功不等于另一台机器已能拉取。只有确认共享分支和远程后才推送，不顺带推送版本标签或触发 Release。
- Mac 接手时从本文“同步代码与冻结候选提交”开始，核对完整 SHA，再执行共同构建、安装和验收步骤；不能只因为 Windows 已通过而跳过 Mac 验收。
- 在 Mac 上发现共享问题，也按同样流程修复、补测试、提交新候选，再让 Windows 对相同候选回归。不要在两台机器各自改版本或各发一个正式 Release。

详细平台资料：[桌面验收](docs/desktop-acceptance.md)、[Windows 验收](docs/windows-acceptance.md)、[Windows 打包](packaging/windows/README.md)、[macOS 打包](packaging/macos/README.md)。
