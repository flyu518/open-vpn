[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProfileA,
    [Parameter(Mandatory = $true)]
    [string]$ProfileB,
    [string]$Daemon = "",
    [string]$QtApp = "",
    [string]$ServiceName = "openvpn",
    [int]$WaitSeconds = 30,
    [switch]$LaunchApplications
)

$ErrorActionPreference = "Stop"

function Get-VpnAdapters {
    @(Get-NetAdapter | Where-Object {
        $_.Name -match "Wintun|OpenVPN|TAP" -or
        $_.InterfaceDescription -match "Wintun|OpenVPN|TAP"
    })
}

function Show-VpnAdapters {
    $adapters = @(Get-VpnAdapters)
    if ($adapters.Count -eq 0) {
        Write-Host "VPN 虚拟网卡：无"
        return
    }
    $adapters | Select-Object Name, InterfaceDescription, Status, ifIndex |
        Format-Table -AutoSize | Out-Host
    foreach ($adapter in $adapters) {
        Get-NetIPConfiguration -InterfaceIndex $adapter.ifIndex -ErrorAction SilentlyContinue |
            Select-Object InterfaceAlias, IPv4Address, IPv6Address, DNSServer |
            Format-List | Out-Host
    }
}

function Wait-ForVpnAdapterCount {
    param(
        [Parameter(Mandatory = $true)] [int]$Minimum,
        [Parameter(Mandatory = $true)] [string]$Description
    )
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    do {
        $up = @(Get-VpnAdapters | Where-Object Status -eq "Up")
        if ($up.Count -ge $Minimum) {
            Write-Host "$Description：已检测到 $($up.Count) 条活动 VPN 虚拟网卡。"
            return $up
        }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)

    Show-VpnAdapters
    throw "$Description 超时：期望至少 $Minimum 条活动 VPN 虚拟网卡，实际 $($up.Count) 条。"
}

function Get-DaemonSessionCount {
    $pipe = $null
    $writer = $null
    $reader = $null
    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(
            ".", "openvpn-ui", [System.IO.Pipes.PipeDirection]::InOut)
        $pipe.Connect(2000)
        $writer = New-Object System.IO.StreamWriter($pipe)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($pipe)
        $writer.WriteLine('{"command":"snapshot"}')
        $response = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($response)) { return $null }
        $snapshot = $response | ConvertFrom-Json
        return @($snapshot.sessions).Count
    } catch {
        return $null
    } finally {
        if ($reader) { $reader.Dispose() }
        if ($writer) { $writer.Dispose() }
        if ($pipe) { $pipe.Dispose() }
    }
}

function Assert-DaemonSessionDelta {
    param(
        [Parameter(Mandatory = $true)] [AllowNull()] [Nullable[int]]$Baseline,
        [Parameter(Mandatory = $true)] [int]$ExpectedDelta,
        [Parameter(Mandatory = $true)] [string]$Description
    )
    if ($null -eq $Baseline) { return }
    $current = Get-DaemonSessionCount
    if ($null -eq $current -or $current -ne ($Baseline + $ExpectedDelta)) {
        throw "$Description：daemon snapshot 会话数异常，期望 $($Baseline + $ExpectedDelta)，实际 $current。"
    }
}

function Assert-VpnAdapterDetails {
    param(
        [Parameter(Mandatory = $true)] [array]$Adapters,
        [Parameter(Mandatory = $true)] [string]$Description
    )
    foreach ($adapter in $Adapters) {
        $config = Get-NetIPConfiguration -InterfaceIndex $adapter.ifIndex -ErrorAction SilentlyContinue
        $addresses = @($config.IPv4Address) + @($config.IPv6Address)
        $routes = @(Get-NetRoute -InterfaceIndex $adapter.ifIndex -ErrorAction SilentlyContinue)
        $dns = @((Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -ErrorAction SilentlyContinue).ServerAddresses)
        Write-Host "$Description：$($adapter.Name) ifIndex=$($adapter.ifIndex) 地址=$($addresses.Count) 路由=$($routes.Count) DNS=$($dns.Count)"
        if ($addresses.Count -eq 0) {
            throw "$Description：虚拟网卡 $($adapter.Name) 没有 IP 地址。"
        }
    }
}

$startedProcesses = @()
$completed = $false
try {
    if (-not (Test-Path -LiteralPath $ProfileA)) { throw "找不到配置 A：$ProfileA" }
    if (-not (Test-Path -LiteralPath $ProfileB)) { throw "找不到配置 B：$ProfileB" }

    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        throw "找不到 Windows Service '$ServiceName'。请先以管理员身份安装 openvpn_windows_service.exe。"
    }
    if ($service.Status -ne "Running") {
        throw "Windows Service '$ServiceName' 当前状态为 $($service.Status)。请先执行 sc.exe start $ServiceName。"
    }

    if ($LaunchApplications) {
        if ([string]::IsNullOrWhiteSpace($Daemon) -or -not (Test-Path -LiteralPath $Daemon)) {
            throw "-LaunchApplications 需要有效的 -Daemon 路径。"
        }
        if ([string]::IsNullOrWhiteSpace($QtApp) -or -not (Test-Path -LiteralPath $QtApp)) {
            throw "-LaunchApplications 需要有效的 -QtApp 路径。"
        }
        $startedProcesses += Start-Process -FilePath $Daemon -PassThru
        $startedProcesses += Start-Process -FilePath $QtApp -PassThru
        Start-Sleep -Seconds 2
    }

    $daemonBaseline = Get-DaemonSessionCount
    Write-Host "Windows 多会话验收开始。请使用本项目 Qt UI；交互必须与 macOS 一致：使用同一个连接列表分别连接配置 A/B。"
    Write-Host "配置 A：$((Resolve-Path -LiteralPath $ProfileA).Path)"
    Write-Host "配置 B：$((Resolve-Path -LiteralPath $ProfileB).Path)"
    Show-VpnAdapters

    Read-Host "请在本项目 Qt UI 导入并连接配置 A；完成后按 Enter"
    $one = Wait-ForVpnAdapterCount -Minimum 1 -Description "配置 A"
    Assert-VpnAdapterDetails -Adapters $one -Description "配置 A"
    Assert-DaemonSessionDelta -Baseline $daemonBaseline -ExpectedDelta 1 -Description "配置 A"
    Show-VpnAdapters

    Read-Host "请在同一个本项目 Qt UI 中连接配置 B；完成后按 Enter"
    $two = Wait-ForVpnAdapterCount -Minimum 2 -Description "配置 A + 配置 B"
    if ($two.Count -lt 2) { throw "第二条连接未创建独立的虚拟网卡。" }
    Assert-VpnAdapterDetails -Adapters $two -Description "配置 A + 配置 B"
    Assert-DaemonSessionDelta -Baseline $daemonBaseline -ExpectedDelta 2 -Description "配置 A + 配置 B"
    Show-VpnAdapters

    Read-Host "请断开配置 B，并保持配置 A 连接；完成后按 Enter"
    $remaining = Wait-ForVpnAdapterCount -Minimum 1 -Description "断开 B 后保留 A"
    if ($remaining.Count -ne 1) {
        throw "断开 B 后活动 VPN 虚拟网卡数量异常：$($remaining.Count)。"
    }
    Assert-VpnAdapterDetails -Adapters $remaining -Description "断开 B 后保留 A"
    Assert-DaemonSessionDelta -Baseline $daemonBaseline -ExpectedDelta 1 -Description "断开 B 后保留 A"
    Show-VpnAdapters

    Read-Host "请断开配置 A；完成后按 Enter"
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    do {
        $active = @(Get-VpnAdapters | Where-Object Status -eq "Up")
        if ($active.Count -eq 0) {
            Write-Host "双会话清理通过：活动 VPN 虚拟网卡已归零。"
            break
        }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)

    if ($active.Count -ne 0) {
        Show-VpnAdapters
        throw "断开 A 后仍有 $($active.Count) 条活动 VPN 虚拟网卡，可能存在会话或路由残留。"
    }
    Assert-DaemonSessionDelta -Baseline $daemonBaseline -ExpectedDelta 0 -Description "双会话清理"

    $completed = $true
    Write-Host "Windows 多会话验收通过：A/B 可同时连接、独立断开，且最终资源清理完成。"
} finally {
    if (-not $completed) {
        foreach ($process in $startedProcesses) {
            if ($process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
        }
        Write-Host "验收未完成：已停止本脚本启动的 Qt/daemon 进程；请检查 Service 和 VPN 资源。"
    }
}
