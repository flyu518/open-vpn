param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$BuildDir = "build\qt-windows",
    [string]$WindowsDaemon = "",
    [string]$WindowsDaemonLauncher = "",
    [string]$WindowsService = "",
    [string]$WintunDll = "",
    [string]$RuntimeDllDir = "",
    [string]$VcRedist = "",
    [string]$Version = "",
    [string]$Output = "",
    [int]$Parallel = 1,
    [switch]$BuildInstaller,
    [switch]$AllowTestWintun
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$versionQuery = @('-DOPENVPN_CONFIG_KEY=version')
if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $versionQuery += "-DOPENVPN_EXPECTED_VERSION=$Version"
}
$sharedVersion = & $cmake @versionQuery -P (Join-Path $root 'cmake/ProjectConfig.cmake')
if ($LASTEXITCODE -ne 0) {
    throw 'Requested version differs from project-config.json or the shared configuration is invalid'
}
$Version = ($sharedVersion | Out-String).Trim()
. (Join-Path $PSScriptRoot 'verify-vc-redist.ps1')
if ($BuildInstaller) {
    $VcRedist = Assert-VisualCppRedistributable $VcRedist
}
$qtCmake = Join-Path $QtDir "bin\qt-cmake.bat"
$qtDeploy = Join-Path $QtDir "bin\windeployqt.exe"
if ([string]::IsNullOrWhiteSpace($Output)) { $Output = "dist\OpenVPN-$Version-windows.zip" }

if (-not (Test-Path $qtCmake)) { throw "找不到 Qt CMake：$qtCmake" }
if (-not (Test-Path $qtDeploy)) { throw "找不到 windeployqt：$qtDeploy" }

function Assert-WintunDll([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "找不到 Wintun DLL：$Path"
    }
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $requiredExports = @(
        "WintunCreateAdapter",
        "WintunOpenAdapter",
        "WintunStartSession",
        "WintunReceivePacket",
        "WintunSendPacket"
    )
    $missing = @($requiredExports | Where-Object { $ascii.IndexOf($_, [StringComparison]::Ordinal) -lt 0 })
    if ($missing.Count -gt 0 -and -not $AllowTestWintun) {
        throw "提供的 Wintun DLL 不是官方 Wintun 二进制，缺少导出：$($missing -join ', ')。正式包禁止使用测试替身；仅离线 UI 测试可显式传入 -AllowTestWintun。"
    }
    if ($missing.Count -gt 0) {
        Write-Warning "使用测试 Wintun 替身，仅允许用于离线 UI 测试：$Path"
        return
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne "Valid" -and -not $AllowTestWintun) {
        throw "Wintun DLL 签名无效：$Path（状态：$($signature.Status)）。正式包必须使用官方签名二进制。"
    }
    if ($signature.SignerCertificate -and
        $signature.SignerCertificate.Subject -notmatch "WireGuard LLC" -and
        -not $AllowTestWintun) {
        throw "Wintun DLL 签名者不是 WireGuard LLC：$($signature.SignerCertificate.Subject)。正式包必须使用官方 Wintun。"
    }
}

Write-Host "正在配置 OpenVPN Qt Windows 桌面版……"
& $qtCmake -S (Join-Path $root "desktop\qt") -B (Join-Path $root $BuildDir) `
    "-DCMAKE_BUILD_TYPE=Release" -UOPENVPN_APP_VERSION "-DOPENVPN_APP_VERSION=$Version"
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 配置失败" }

Write-Host "正在编译 OpenVPN Qt Windows 桌面版……"
if ($Parallel -lt 1) { throw "Parallel 必须大于等于 1" }
$buildDirectory = Join-Path $root $BuildDir
$solution = Join-Path $buildDirectory "OpenVPNQt.sln"
if (Test-Path -LiteralPath $solution -PathType Leaf) {
    # Use MSBuild directly for the Visual Studio generator. CMake's build
    # wrapper can leave MSBuild idle in managed Windows shells without
    # returning a completion status.
    $msbuildExecutable = $null
    $msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $msbuildCommand) {
        $msbuildExecutable = $msbuildCommand.Source
    } else {
        $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) `
            'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $msbuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
                -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            if ($msbuildPath -and (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
                $msbuildExecutable = (Resolve-Path -LiteralPath $msbuildPath).Path
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($msbuildExecutable)) {
        throw "找不到 Visual Studio MSBuild.exe，无法编译 Qt Windows 桌面版。"
    }
    & $msbuildExecutable $solution '/t:openvpn_qt' "/p:Configuration=Release" "/p:Platform=x64" `
        "/m:$Parallel" "/v:minimal" "/nologo"
} else {
    & $cmake --build $buildDirectory --config Release --target openvpn_qt -- /m:$Parallel /v:minimal
}
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 编译失败" }

$app = Join-Path $root "$BuildDir\Release\openvpn_qt.exe"
if (-not (Test-Path $app)) { throw "找不到 Qt Windows 应用：$app" }

Write-Host "正在部署 Qt 运行时……"
# Qt 6.8's qmlimportscanner can terminate unexpectedly when scanning this
# project through windeployqt. Deploy the native runtime first, then copy the
# known QML modules and their Windows DLLs explicitly. This is also the same
# layout used by the Windows acceptance bundle tested in this repository.
& $qtDeploy --release --no-quick-import --no-translations $app
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 运行时部署失败" }
$releaseDir = Split-Path -Parent $app
$qmlDestination = Join-Path $releaseDir "qml"
New-Item -ItemType Directory -Force -Path $qmlDestination | Out-Null
foreach ($qmlModule in @("Qt", "QtQml", "QtQuick")) {
    $qmlSource = Join-Path $QtDir "qml\$qmlModule"
    if (-not (Test-Path $qmlSource)) { throw "找不到 Qt QML 模块：$qmlSource" }
    Copy-Item -Recurse -Force $qmlSource $qmlDestination
}
foreach ($dllPattern in @("Qt6Qml*.dll", "Qt6Quick*.dll")) {
    Get-ChildItem (Join-Path $QtDir "bin") -Filter $dllPattern -File |
        Copy-Item -Force -Destination $releaseDir
}

# Keep the staging directory beside the build tree. Some managed Windows
# environments apply restrictive ACLs to %TEMP%, which can allow copying but
# reject an otherwise harmless rename of the deployed executable.
$stage = Join-Path $root ("build\openvpn-package-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$stage = (Resolve-Path -LiteralPath $stage).Path
$stageParent = (Resolve-Path -LiteralPath (Join-Path $root 'build')).Path.TrimEnd('\') + '\'
if (-not $stage.StartsWith($stageParent, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $stage) -notmatch '^openvpn-package-[0-9a-f]{32}$') {
    throw "打包暂存路径不在预期的 build 子目录内：$stage"
}
try {
    $releaseRoot = Join-Path $root "$BuildDir\Release"
    $releaseItems = Get-ChildItem -LiteralPath $releaseRoot -Force
    foreach ($item in $releaseItems) {
        # The Release directory also contains test executables, linker/import
        # files and debug symbols when the full Qt test build ran. None of
        # those are runtime inputs and shipping them makes the bundle look as
        # if it contains another application.
        if ($item.Name -like "*_tests.exe" -or
            $item.Name -like "*.previous.exe" -or
            $item.Name -like "*.pdb" -or
            $item.Name -like "*.ilk" -or
            $item.Name -like "*.exp" -or
            $item.Name -like "*.lib" -or
            $item.Name -like "*.lock*" -or
            $item.Name -like "*d.dll" -or
            $item.Name -like "Qt6QuickTest*.dll") {
            continue
        }
        Copy-Item -Recurse -Force -LiteralPath $item.FullName -Destination $stage
    }
    $staleFiles = Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
        $_.Name -eq "install-iexpress.cmd" -or
        $_.Name -like "*.previous.exe" -or $_.Name -like "*.pdb" -or
        $_.Name -like "*.ilk" -or $_.Name -like "*.obj" -or
        $_.Name -like "*.lock*" -or $_.Name -like "*d.dll" -or
        $_.Name -like "Qt6QuickTest*.dll"
    }
    if ($staleFiles) { $staleFiles | Remove-Item -Force }
    # windeployqt's Qt directory can contain Qt's development-only test
    # plugin objects. They are not runtime dependencies and must never ship.
    $qtTestModules = Join-Path $stage "qml\Qt\test"
    if (Test-Path -LiteralPath $qtTestModules -PathType Container) {
        if (-not (Resolve-Path -LiteralPath $qtTestModules).Path.StartsWith(
            $stage + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Qt 测试模块路径超出暂存目录，拒绝删除'
        }
        Remove-Item -LiteralPath $qtTestModules -Recurse -Force
    }
    if (-not (Test-Path (Join-Path $stage "openvpn_qt.exe"))) {
        throw "打包暂存目录缺少 Qt Windows 应用"
    }
    Rename-Item (Join-Path $stage "openvpn_qt.exe") "OpenVPN.exe"

    $runtime = @{
        "Windows daemon" = $WindowsDaemon
        "Windows daemon launcher" = $WindowsDaemonLauncher
        "Windows service" = $WindowsService
        "Wintun" = $WintunDll
    }
    foreach ($item in $runtime.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace($item.Value)) {
            throw "生成 Windows 包必须提供 $($item.Key) 路径"
        }
        if (-not (Test-Path $item.Value)) { throw "找不到 $($item.Key)：$($item.Value)" }
        if ($item.Key -eq "Wintun") { Assert-WintunDll $item.Value }
        $targetName = switch ($item.Key) {
            "Windows daemon" { "openvpn_windows_daemon.exe" }
            "Windows daemon launcher" { "openvpn_windows_daemon_launcher.exe" }
            "Windows service" { "openvpn_windows_service.exe" }
            "Wintun" { "wintun.dll" }
        }
        Copy-Item -Force $item.Value (Join-Path $stage $targetName)
    }

    if ([string]::IsNullOrWhiteSpace($RuntimeDllDir)) {
        $RuntimeDllDir = Split-Path -Parent (Resolve-Path $WindowsDaemon)
    }
    $runtimeDlls = @(
        "jsoncpp.dll",
        "lz4.dll",
        "libcrypto-3-x64.dll",
        "libssl-3-x64.dll"
    )
    foreach ($runtimeDll in $runtimeDlls) {
        $source = Join-Path $RuntimeDllDir $runtimeDll
        if (Test-Path $source) {
            Copy-Item -Force $source (Join-Path $stage $runtimeDll)
        }
    }
    $optionalFmt = Join-Path $RuntimeDllDir "fmt.dll"
    if (Test-Path $optionalFmt) {
        Copy-Item -Force $optionalFmt (Join-Path $stage "fmt.dll")
    }
    $missingRuntimeDlls = @($runtimeDlls | Where-Object {
        -not (Test-Path (Join-Path $stage $_))
    })
    if ($missingRuntimeDlls.Count -gt 0) {
        throw "Windows 运行时 DLL 缺失：$($missingRuntimeDlls -join ', ')。请设置 -RuntimeDllDir 指向 vcpkg/bin。"
    }

    $outputPath = Join-Path $root $Output
    New-Item -ItemType Directory -Force -Path (Split-Path $outputPath) | Out-Null
    if ($BuildInstaller) {
        $installerPath = [IO.Path]::ChangeExtension($outputPath, ".exe")
        $isccExecutable = $null
        $isccCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($null -ne $isccCommand) {
            $isccExecutable = $isccCommand.Source
        } else {
            $isccCandidates = @(
                (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Inno Setup 6\ISCC.exe'),
                (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Inno Setup 6\ISCC.exe'),
                (Join-Path $env:USERPROFILE 'AppData\Local\Programs\Inno Setup 6\ISCC.exe'),
                (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
            )
            $isccPath = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
            if ($isccPath) { $isccExecutable = (Resolve-Path -LiteralPath $isccPath).Path }
        }
        if ([string]::IsNullOrWhiteSpace($isccExecutable)) {
            throw "找不到 Inno Setup 6 编译器 ISCC.exe。请先安装 Inno Setup 6，再重新打包。"
        }

        $installerDirectory = Split-Path -Parent $installerPath
        New-Item -ItemType Directory -Force -Path $installerDirectory | Out-Null
        $installerScript = Join-Path $PSScriptRoot 'OpenVPN.iss'
        $compilerArguments = @(
            "/DAppVersion=$Version",
            "/DVcRedistPath=$($VcRedist.Replace('\', '/'))",
            "/DStageDir=$($stage.Replace('\', '/'))",
            "/DOutputDirectory=$((Resolve-Path -LiteralPath $installerDirectory).Path.Replace('\', '/'))",
            "/DOutputBaseFilename=$([IO.Path]::GetFileNameWithoutExtension($installerPath))",
            $installerScript
        )
        & $isccExecutable @compilerArguments
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup 安装器编译失败（ISCC exit code $LASTEXITCODE）" }
        if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf) -or
            (Get-Item -LiteralPath $installerPath).Length -eq 0) {
            throw "Inno Setup 未生成有效安装程序：$installerPath"
        }
        Write-Host "OpenVPN Windows x64 标准安装程序已生成：$installerPath"
    } else {
        if (Test-Path $outputPath) { Remove-Item -Force $outputPath }
        Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $outputPath
        Write-Host "OpenVPN Windows Qt 压缩包已生成：$outputPath"
    }
}
finally {
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        if ($resolvedStage -ne $stage -or
            -not $resolvedStage.StartsWith($stageParent, [StringComparison]::OrdinalIgnoreCase) -or
            ((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw '暂存目录身份发生变化，拒绝递归删除'
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
