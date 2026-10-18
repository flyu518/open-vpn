param(
    [string]$DaemonSource = "core/tools/windows_daemon_main.cpp"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($DaemonSource)) { $DaemonSource = Join-Path $repoRoot $DaemonSource }
$path = (Resolve-Path -LiteralPath $DaemonSource).Path
$content = Get-Content -Raw -LiteralPath $path

if ($content -notmatch "GetTokenInformation\s*\(\s*token\s*,\s*TokenUser") {
    throw "The daemon named pipe must derive its access rule from the launching user's token."
}
if ($content -notmatch "SetEntriesInAclW\s*\(") {
    throw "The daemon named pipe must add an explicit ACE for the launching user."
}
if ($content -notmatch "CreateNamedPipeW\s*\([\s\S]*?pipe_security\.attributes\(\)\s*\)") {
    throw "CreateNamedPipeW must receive the explicit per-user security descriptor."
}

Write-Output "Named pipe per-user ACL source checks passed: $path"
