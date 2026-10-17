param(
    [string]$QtDir = "C:\Qt\6.12.0\msvc2022_64",
    [string]$BuildDir = "build\qt-windows",
    [string]$WindowsDaemon = "",
    [string]$WindowsService = "",
    [string]$WintunDll = "",
    [string]$Version = "0.0.2",
    [string]$Output = "",
    [switch]$BuildInstaller
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$qtCmake = Join-Path $QtDir "bin\qt-cmake.bat"
$qtDeploy = Join-Path $QtDir "bin\windeployqt.exe"
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
if ([string]::IsNullOrWhiteSpace($Output)) { $Output = "dist\OpenVPN-$Version-windows.zip" }

if (-not (Test-Path $qtCmake)) { throw "找不到 Qt CMake：$qtCmake" }
if (-not (Test-Path $qtDeploy)) { throw "找不到 windeployqt：$qtDeploy" }

Write-Host "正在配置 OpenVPN Qt Windows 桌面版……"
& $qtCmake -S (Join-Path $root "desktop\qt") -B (Join-Path $root $BuildDir) `
    -DCMAKE_BUILD_TYPE=Release -DOPENVPN_APP_VERSION=$Version
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 配置失败" }

Write-Host "正在编译 OpenVPN Qt Windows 桌面版……"
& $cmake --build (Join-Path $root $BuildDir) --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 编译失败" }

$app = Join-Path $root "$BuildDir\Release\openvpn_qt.exe"
if (-not (Test-Path $app)) { throw "找不到 Qt Windows 应用：$app" }

Write-Host "正在部署 Qt 运行时……"
& $qtDeploy --release --qmldir (Join-Path $root "desktop\qt\qml") $app
if ($LASTEXITCODE -ne 0) { throw "Qt Windows 运行时部署失败" }

$stage = Join-Path $env:TEMP ("openvpn-package-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Copy-Item -Recurse -Force (Join-Path $root "$BuildDir\Release\*") $stage
    Rename-Item (Join-Path $stage "openvpn_qt.exe") "OpenVPN.exe"

    $runtime = @{
        "Windows daemon" = $WindowsDaemon
        "Windows service" = $WindowsService
        "Wintun" = $WintunDll
    }
    foreach ($item in $runtime.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace($item.Value)) {
            throw "生成 Windows 包必须提供 $($item.Key) 路径"
        }
        if (-not (Test-Path $item.Value)) { throw "找不到 $($item.Key)：$($item.Value)" }
        $targetName = switch ($item.Key) {
            "Windows daemon" { "openvpn_windows_daemon.exe" }
            "Windows service" { "openvpn_windows_service.exe" }
            "Wintun" { "wintun.dll" }
        }
        Copy-Item -Force $item.Value (Join-Path $stage $targetName)
    }

    $outputPath = Join-Path $root $Output
    New-Item -ItemType Directory -Force -Path (Split-Path $outputPath) | Out-Null
    if (Test-Path $outputPath) { Remove-Item -Force $outputPath }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $outputPath
    Write-Host "OpenVPN Windows Qt 压缩包已生成：$outputPath"

    if ($BuildInstaller) {
        $makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
        if ($null -eq $makensis) { throw "找不到 makensis.exe，请先安装 NSIS" }
        $installerPath = [IO.Path]::ChangeExtension($outputPath, ".exe")
        $stageForNsis = $stage.Replace("\", "/")
        $installerForNsis = $installerPath.Replace("\", "/")
        $nsiPath = Join-Path $env:TEMP ("openvpn-" + [Guid]::NewGuid().ToString("N") + ".nsi")
        $nsiContent = @'
!include "MUI2.nsh"
!include "LogicLib.nsh"
Unicode true
Name "OpenVPN"
OutFile "__OUTPUT__"
InstallDir "$PROGRAMFILES64\OpenVPN"
RequestExecutionLevel admin
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Section "OpenVPN"
  nsExec::ExecToLog '"$SYSDIR\sc.exe" stop openvpn'
  Pop $0
  SetOutPath "$INSTDIR"
  File /r "__STAGE__\*"
  nsExec::ExecToLog '"$SYSDIR\sc.exe" query openvpn'
  Pop $0
  ${If} $0 == 0
    nsExec::ExecToLog '"$SYSDIR\sc.exe" config openvpn binPath= $\"$INSTDIR\openvpn_windows_service.exe$\" start= auto'
  ${Else}
    nsExec::ExecToLog '"$INSTDIR\openvpn_windows_service.exe" install'
  ${EndIf}
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "OpenVPN 服务注册失败。请查看安装日志。"
    Abort
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\sc.exe" start openvpn'
  Pop $0
  ${If} $0 != 0
  ${AndIf} $0 != 1056
    MessageBox MB_ICONSTOP "OpenVPN 服务启动失败。请查看安装日志。"
    Abort
  ${EndIf}
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "OpenVPNDaemon" '"$INSTDIR\openvpn_windows_daemon.exe"'
  Exec '"$INSTDIR\openvpn_windows_daemon.exe"'
  CreateShortCut "$DESKTOP\OpenVPN.lnk" "$INSTDIR\OpenVPN.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd
Section "Uninstall"
  nsExec::ExecToLog '"$SYSDIR\sc.exe" stop openvpn'
  Pop $0
  nsExec::ExecToLog '"$INSTDIR\openvpn_windows_service.exe" remove'
  Pop $0
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "OpenVPNDaemon"
  Delete "$DESKTOP\OpenVPN.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
'@
        $nsiContent = $nsiContent.Replace("__STAGE__", $stageForNsis).Replace("__OUTPUT__", $installerForNsis)
        $nsiContent | Set-Content -Encoding UTF8 $nsiPath
        & $makensis.Source $nsiPath
        if ($LASTEXITCODE -ne 0) { throw "NSIS 安装器生成失败" }
        Remove-Item -Force $nsiPath
        Write-Host "OpenVPN Windows 安装器已生成：$installerPath"
    }
}
finally {
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
}
