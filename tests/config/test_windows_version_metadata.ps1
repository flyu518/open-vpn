param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$config = Get-Content -LiteralPath (Join-Path $repo 'project-config.json') -Raw | ConvertFrom-Json
$info = (Get-Item -LiteralPath $Executable).VersionInfo
if ($info.ProductVersion -ne $config.version -or $info.FileVersion -ne $config.version) {
    throw "EXE metadata must match project-config.json $($config.version); product='$($info.ProductVersion)' file='$($info.FileVersion)'"
}
Write-Output 'Windows EXE version metadata matches shared configuration'
