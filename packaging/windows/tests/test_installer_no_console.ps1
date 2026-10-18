param(
    [string]$PackageScript = "packaging/windows/build-qt-package.ps1",
    [string]$InstallerScript = "packaging/windows/OpenVPN.iss"
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not [IO.Path]::IsPathRooted($PackageScript)) { $PackageScript = Join-Path $repoRoot $PackageScript }
if (-not [IO.Path]::IsPathRooted($InstallerScript)) { $InstallerScript = Join-Path $repoRoot $InstallerScript }
$packagePath = (Resolve-Path -LiteralPath $PackageScript).Path
$installerPath = (Resolve-Path -LiteralPath $InstallerScript).Path
$package = Get-Content -Raw -LiteralPath $packagePath
$installer = Get-Content -Raw -LiteralPath $installerPath
$languagePath = Join-Path (Split-Path -Parent $installerPath) "OpenVPN.zh-CN.isl"

if ($package -notmatch "ISCC\.exe" -or $package -notmatch "OpenVPN\.iss") {
    throw "The package builder must compile the standard Inno Setup installer."
}
if ($package -match '&\s*\$(msbuild|iscc)\.Source' -or
    $package -notmatch '\$msbuildExecutable' -or
    $package -notmatch '\$isccExecutable') {
    throw "The package builder must invoke resolved MSBuild and Inno compiler paths instead of assuming command objects."
}
if ($package -match "WindowsInstallerStub|OVPNZIP1|openvpn_windows_installer\.exe") {
    throw "The package builder must not emit the legacy self-extracting console installer."
}
if ($package -notmatch 'qml\\Qt\\test' -or $package -notmatch '\*\.obj') {
    throw "The package builder must exclude Qt test modules and intermediate object files from the runtime installer."
}
if ($package -notmatch '\$_\.Name\s+-eq\s+"install-iexpress\.cmd"') {
    throw "The package builder must exclude the obsolete IExpress installer script from the staged application."
}
if (-not (Test-Path -LiteralPath $languagePath -PathType Leaf)) {
    throw "The installer must ship its Chinese language overlay with the source."
}
$language = Get-Content -Raw -LiteralPath $languagePath
if ($installer -notmatch "WizardStyle\s*=\s*modern" -or
    $installer -notmatch "PrivilegesRequired\s*=\s*admin" -or
    $installer -notmatch "UsePreviousLanguage\s*=\s*no" -or
    $installer -notmatch "LanguageDetectionMethod\s*=\s*none" -or
    $installer -notmatch 'Name:\s*"chinesesimp";\s*MessagesFile:\s*"OpenVPN\.zh-CN\.isl"' -or
    $installer -notmatch "MissingMessagesWarning\s*=\s*yes" -or
    $installer -notmatch "NotRecognizedMessagesWarning\s*=\s*yes" -or
    $language -notmatch "LanguageName=简体中文" -or
    $language -notmatch 'LanguageID=\$0804' -or
    $language -notmatch "LanguageCodePage=936" -or
    $language -notmatch "WelcomeLabel1=欢迎使用 \[name\] 安装向导" -or
    $language -notmatch "WizardSelectTasks=选择附加任务" -or
    $language -notmatch "SelectTasksDesc=您想要安装程序执行哪些附加任务？" -or
    $language -notmatch "SelectTasksLabel2=选择您想要安装程序在安装 \[name\] 时执行的附加任务" -or
    $language -notmatch "WizardSelectDir=选择目标位置" -or
    $language -notmatch "WizardReady=准备安装" -or
    $language -notmatch "WizardInstalling=正在安装" -or
    $language -notmatch "FinishedHeadingLabel=完成 \[name\] 安装向导" -or
    $language -notmatch "ButtonNext=下一步" -or
    $language -notmatch "ButtonCancel=取消" -or
    $language -notmatch "ButtonFinish=完成" -or
    $installer -match "WizardFinished=" -or
    $installer -match 'compiler:Default\.isl' -or
    $installer -notmatch "recursesubdirs\s+createallsubdirs") {
    throw "The installer must be a modern, elevated, fully Chinese-language wizard that embeds the application files."
}
if ($installer -notmatch '(?s)\[InstallDelete\].*?Name:\s*"\{app\}\\install-iexpress\.cmd"') {
    throw "Upgrades must delete the obsolete IExpress installer script from existing installations."
}
if ($installer -match "cmd\.exe|Expand-Archive") {
    throw "The installer must not expose a command window or use a shell-based extraction stage."
}

Write-Output "Standard installer wizard and no-console packaging checks passed: $installerPath"
