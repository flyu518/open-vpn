param(
    [Parameter(Mandatory = $true)]
    [string]$EngineSource,
    [Parameter(Mandatory = $true)]
    [string]$ConfigHeader
)

$ErrorActionPreference = "Stop"
$source = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $EngineSource)
$config = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $ConfigHeader)
if ($source -notmatch '(?s)openvpn3_config::connection_timeout_seconds' -or
    $source -notmatch '(?s)openvpn3_config::retry_on_auth_failed' -or
    $config -notmatch '(?s)connection_timeout_seconds\s*=\s*[1-9][0-9]*') {
    throw "Windows OpenVPN3 engine must configure a finite connection timeout."
}

Write-Output "Windows connection timeout configuration verified."
