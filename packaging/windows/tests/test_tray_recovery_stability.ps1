param(
    [string]$TraySource = "desktop/qt/src/tray_controller.cpp"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($TraySource)) { $TraySource = Join-Path $repoRoot $TraySource }
$path = (Resolve-Path -LiteralPath $TraySource).Path
$content = Get-Content -Raw -LiteralPath $path
$start = $content.IndexOf("bool TrayController::recoverTrayIcon()", [StringComparison]::Ordinal)
$end = $content.IndexOf("void TrayController::scheduleTrayRecovery()", $start, [StringComparison]::Ordinal)
if ($start -lt 0 -or $end -lt 0) {
    throw "Could not locate TrayController::recoverTrayIcon."
}
$body = $content.Substring($start, $end - $start)
$reuse = $body.IndexOf("if (tray_ && tray_->isVisible())", [StringComparison]::Ordinal)
$delete = $body.IndexOf("delete tray_", [StringComparison]::Ordinal)
if ($reuse -lt 0 -or $delete -lt 0 -or $reuse -gt $delete) {
    throw "Routine tray recovery must preserve an already visible icon instead of recreating it."
}

Write-Output "Tray recovery stability check passed: $path"
