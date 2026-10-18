$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$installer = Get-Content -LiteralPath (Join-Path $repo 'packaging/windows/OpenVPN.iss') -Raw
$builder = Get-Content -LiteralPath (Join-Path $repo 'packaging/windows/build-qt-package.ps1') -Raw
if ($installer -notmatch 'Source:\s*"\{#VcRedistPath\}";\s*DestDir:\s*"\{tmp\}"' -or
    $installer -notmatch 'EnsureVisualCppRuntime;\s*InstallOrUpdateService;' -or
    $installer -notmatch '/install /quiet /norestart' -or
    $installer -notmatch 'if VisualCppRuntimeReady then' -or
    $installer -notmatch 'RegQueryDWordValue\(HKLM64' -or
    $builder -notmatch '/DVcRedistPath=' -or
    $builder -notmatch 'Assert-VisualCppRedistributable') {
    throw 'The installer must embed a validated offline VC++ x64 runtime and install it before starting the VPN service.'
}
$verifier = Join-Path $repo 'packaging/windows/verify-vc-redist.ps1'
if (-not (Test-Path -LiteralPath $verifier)) { throw 'VC++ package verification is missing' }
. $verifier
try {
    Assert-VisualCppRedistributable $PSCommandPath
    throw 'An incomplete redistributable was accepted'
} catch {
    if ($_.Exception.Message -notmatch '完整离线') { throw }
}
try {
    Assert-VisualCppRedistributable (Join-Path $repo 'build/nonexistent-vc-redist.x64.exe')
    throw 'A missing VC++ redistributable was accepted'
} catch {
    if ($_.Exception.Message -notmatch '找不到 VC\+\+') { throw }
}
Write-Output 'Offline VC++ runtime packaging and installation ordering checks passed'
