$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$config = Get-Content -LiteralPath (Join-Path $repo 'project-config.json') -Raw | ConvertFrom-Json
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$version = & $cmake -DOPENVPN_CONFIG_KEY=version -P (Join-Path $repo 'cmake/ProjectConfig.cmake')
if ($LASTEXITCODE -ne 0 -or $version.Trim() -ne $config.version) { throw 'Shared version query failed' }
try {
    & (Join-Path $repo 'packaging/windows/build-qt-package.ps1') -Version "$($config.version).invalid" -QtDir 'missing-test-sdk'
    throw 'Mismatched packaging version was accepted'
} catch {
    if ($_.Exception.Message -notmatch 'differs from project-config.json') { throw }
}
$errors = $null
$tokens = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $repo 'packaging/windows/build-qt-package.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
Write-Output 'Windows shared packaging configuration tests passed'
