param(
    [string]$Source = "core/tools/windows_daemon_main.cpp"
)

$ErrorActionPreference = "Stop"
$content = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Source)
foreach ($required in @(
    "OpenSCManagerW",
    "OpenServiceW",
    "SERVICE_QUERY_STATUS",
    "SERVICE_RUNNING",
    "windows_service_unavailable",
    "Windows OpenVPN Service",
    'command == "ping" || command == "connect"'
)) {
    if ($content.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
        throw "Windows daemon runtime preflight is missing $required."
    }
}

Write-Output "Windows daemon service preflight is present before connect handling."
