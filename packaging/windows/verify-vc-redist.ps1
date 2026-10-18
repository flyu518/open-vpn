function Assert-VisualCppRedistributable([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw '找不到 VC++ x64 运行库安装程序，请通过 -VcRedist 提供微软官方 VC_redist.x64.exe。'
    }
    $item = Get-Item -LiteralPath $Path
    # Burn's Package Cache can contain a signed ~600 KiB engine-only stub,
    # not the complete Microsoft download with its attached MSI payloads.
    # Fail closed on these stubs before accepting signature/version metadata.
    if ($item.Length -lt 5MB) {
        throw 'VC++ 运行库不是完整离线安装包；请使用微软官网下载的完整 EXE，不要使用 Package Cache 中的缓存启动器。'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $item.FullName
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation' -or
        $item.VersionInfo.FileDescription -notmatch '\(x64\)' -or
        $item.VersionInfo.OriginalFilename -ine 'VC_redist.x64.exe') {
        throw 'VC++ 运行库必须是签名有效的微软官方 x64 Redistributable。'
    }
    $runtimeVersion = [version]$item.VersionInfo.ProductVersion.Trim()
    if ($runtimeVersion -lt [version]'14.43.0.0') {
        throw 'VC++ 运行库版本过旧；本项目 MSVC 工具链需要 14.43 或更新的 x64 运行库。'
    }
    return $item.FullName
}
