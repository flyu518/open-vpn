param(
    [string]$MainSource = "desktop/qt/src/main.cpp",
    [string]$InstallerScript = "packaging/windows/OpenVPN.iss"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($MainSource)) { $MainSource = Join-Path $repoRoot $MainSource }
if (-not [IO.Path]::IsPathRooted($InstallerScript)) { $InstallerScript = Join-Path $repoRoot $InstallerScript }
$mainPath = (Resolve-Path -LiteralPath $MainSource).Path
$installerPath = (Resolve-Path -LiteralPath $InstallerScript).Path
$main = Get-Content -Raw -LiteralPath $mainPath
$installer = Get-Content -Raw -LiteralPath $installerPath

if ($main -notmatch "ensureWindowsDaemonReady\s*\(\)" -or
    $main -notmatch "QLocalSocket" -or
    $main -notmatch "QProcess::startDetached" -or
    $main -notmatch "openvpn_windows_daemon_launcher\.exe") {
    throw "The desktop app must ensure its same-user daemon is ready before connecting to the named pipe."
}
$bootstrapCall = $main.IndexOf("ensureWindowsDaemonReady();", [StringComparison]::Ordinal)
$controllerStart = $main.IndexOf("controller.start();", [StringComparison]::Ordinal)
if ($bootstrapCall -lt 0 -or $controllerStart -lt 0 -or $bootstrapCall -gt $controllerStart) {
    throw "The daemon bootstrap must run before AppController starts its first service request."
}
if ($installer -match 'ValueName:\s*"OpenVPNDaemon"') {
    throw "The elevated installer must not write the per-user daemon startup value."
}
if ($installer -notmatch 'Filename:\s*"\{app\}\\OpenVPN\.exe"[^\r\n]*postinstall[^\r\n]*runasoriginaluser') {
    throw "The installer must launch the desktop UI as the original, non-elevated user."
}

Write-Output "Same-user daemon bootstrap and installer launch checks passed."
