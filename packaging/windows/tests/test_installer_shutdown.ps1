param(
    [string]$InstallerScript = "packaging/windows/OpenVPN.iss"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($InstallerScript)) { $InstallerScript = Join-Path $repoRoot $InstallerScript }
$path = (Resolve-Path -LiteralPath $InstallerScript).Path
$content = Get-Content -Raw -LiteralPath $path
$prepareStart = $content.IndexOf('function PrepareToInstall', [StringComparison]::OrdinalIgnoreCase)
$prepareEnd = $content.IndexOf('end;', $prepareStart, [StringComparison]::OrdinalIgnoreCase)
if ($prepareStart -lt 0 -or $prepareEnd -lt 0) {
    throw "Inno Setup must stop the previous installation from PrepareToInstall before replacing files."
}
$prepare = $content.Substring($prepareStart, $prepareEnd - $prepareStart)
$serviceStopCall = $prepare.IndexOf('StopOpenVpnService(Result)', [StringComparison]::OrdinalIgnoreCase)
$processStopCall = $prepare.IndexOf('StopDesktopProcesses', [StringComparison]::OrdinalIgnoreCase)
$serviceFunctionStart = $content.IndexOf('function StopOpenVpnService', [StringComparison]::OrdinalIgnoreCase)
$processFunctionStart = $content.IndexOf('procedure StopDesktopProcesses', [StringComparison]::OrdinalIgnoreCase)
$serviceFunction = $content.Substring($serviceFunctionStart, $processFunctionStart - $serviceFunctionStart)
$processFunctionEnd = $content.IndexOf('function PrepareToInstall', $processFunctionStart, [StringComparison]::OrdinalIgnoreCase)
$processFunction = $content.Substring($processFunctionStart, $processFunctionEnd - $processFunctionStart)
$launcherStop = $processFunction.IndexOf('openvpn_windows_daemon_launcher.exe', [StringComparison]::OrdinalIgnoreCase)
$daemonStop = $processFunction.IndexOf('openvpn_windows_daemon.exe', [StringComparison]::OrdinalIgnoreCase)
$uiStop = $processFunction.IndexOf('OpenVPN.exe', [StringComparison]::OrdinalIgnoreCase)
if ($serviceStopCall -lt 0 -or $processStopCall -lt 0 -or
    $serviceFunction -notmatch 'ControlService\(' -or
    $serviceFunction -notmatch 'SERVICE_STOPPED' -or
    $launcherStop -lt 0 -or $daemonStop -lt 0 -or $uiStop -lt 0 -or
    $launcherStop -gt $daemonStop -or $daemonStop -gt $uiStop) {
    throw "Installer must stop the service, daemon and desktop UI before Inno Setup copies replacement files."
}

Write-Output "Installer shutdown ordering verified: old service/daemon/UI stop in PrepareToInstall."
