param(
    [string]$Script = "packaging/windows/build-qt-package.ps1",
    [string]$FakeDll = "tmp/mock-wintun.dll"
)

$ErrorActionPreference = "Stop"
$path = (Resolve-Path -LiteralPath $Script).Path
$content = Get-Content -Raw -LiteralPath $path
foreach ($symbol in @(
    "WintunCreateAdapter",
    "WintunOpenAdapter",
    "WintunStartSession",
    "WintunReceivePacket",
    "WintunSendPacket"
)) {
    if ($content.IndexOf($symbol, [StringComparison]::Ordinal) -lt 0) {
        throw "Package script does not validate Wintun export $symbol."
    }
}
if ($content.IndexOf("AllowTestWintun", [StringComparison]::Ordinal) -lt 0) {
    throw "Package script must keep test Wintun bypass explicit."
}

$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $FakeDll))
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
if ($ascii.IndexOf("WintunCreateAdapter", [StringComparison]::Ordinal) -ge 0) {
    throw "The known fake Wintun DLL unexpectedly contains a real Wintun export."
}

Write-Output "Wintun package validation rejects the known fake DLL and keeps test bypass explicit."
