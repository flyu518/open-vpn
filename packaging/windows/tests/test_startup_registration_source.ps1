param(
    [string]$ControllerSource = "desktop/qt/src/app_controller.cpp"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($ControllerSource)) {
    $ControllerSource = Join-Path $repoRoot $ControllerSource
}
$source = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $ControllerSource).Path

if ($source -notmatch 'QStandardPaths::ApplicationsLocation' -or
    $source -notmatch 'Startup[/\\]OpenVPN\.lnk' -or
    $source -notmatch 'QFile::link\s*\(' -or
    $source -notmatch 'QFile::symLinkTarget\s*\(') {
    throw "Windows login startup must use a verified per-user Startup-folder shortcut."
}

$windowsWriteState = [regex]::Match(
    $source,
    '(?s)bool AppController::writeStartupState\(bool enabled, QString\* error\) const \{(.*?)#elif defined\(Q_OS_MACOS\)')
if (-not $windowsWriteState.Success -or
    $windowsWriteState.Groups[1].Value -notmatch 'QSettings' -or
    $windowsWriteState.Groups[1].Value -notmatch 'settings\.remove\(QStringLiteral\("OpenVPN"\)\)' -or
    $windowsWriteState.Groups[1].Value.IndexOf('writeStartupShortcut') -gt
        $windowsWriteState.Groups[1].Value.IndexOf('settings.remove')) {
    throw "Windows startup must migrate and remove the legacy Run entry after creating the shortcut."
}

Write-Output "Windows Startup-folder registration and legacy Run migration checks passed."
