param(
    [string]$Daemon = "build/core-windows/Release/openvpn_windows_daemon.exe",
    [string]$Profile = ""
)

$ErrorActionPreference = "Stop"
$daemonPath = (Resolve-Path -LiteralPath $Daemon).Path
$workDir = Split-Path -Parent $daemonPath
$pipeName = "openvpn-preflight-" + [Guid]::NewGuid().ToString("N")
$pipePath = "\\.\pipe\$pipeName"
$process = $null
$client = $null
$writer = $null
$reader = $null
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $daemonPath
    $startInfo.Arguments = '--pipe "' + $pipePath + '"'
    $startInfo.WorkingDirectory = $workDir
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    Start-Sleep -Milliseconds 250
    if ($process.HasExited) { throw "daemon exited before opening its pipe (exit code $($process.ExitCode))" }
    $connected = $false
    for ($attempt = 0; $attempt -lt 20 -and -not $connected; $attempt++) {
        $candidate = [IO.Pipes.NamedPipeClientStream]::new(".", $pipeName, [IO.Pipes.PipeDirection]::InOut)
        try {
            $candidate.Connect(500)
            $client = $candidate
            $connected = $true
        } catch [TimeoutException] {
            $candidate.Dispose()
            if ($process.HasExited) { throw "daemon exited before opening its pipe (exit code $($process.ExitCode))" }
        }
    }
    if (-not $connected) { throw "daemon did not open its pipe within 10 seconds" }
    $writer = [IO.StreamWriter]::new($client, [Text.UTF8Encoding]::new($false), 8192, $true)
    $writer.AutoFlush = $true
    $reader = [IO.StreamReader]::new($client, [Text.UTF8Encoding]::new($false), $false, 8192, $true)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $writer.WriteLine('{"command":"ping"}')
    $pingResponse = $reader.ReadLine()
    $pingJson = $pingResponse | ConvertFrom-Json
    if ($pingJson.ok -eq $true) {
        # Keep this assertion ASCII-only so Windows PowerShell 5 can parse this
        # UTF-8-without-BOM script without corrupting the expected string.
        if ($pingJson.message -notlike "OpenVPN*") {
            throw "unexpected healthy-service ping response: $pingResponse"
        }
        if ($watch.Elapsed.TotalSeconds -ge 3) {
            throw "healthy-service preflight was not immediate: $($watch.Elapsed.TotalSeconds.ToString('0.000')) s"
        }
        Write-Output "Daemon pipe ACL and running-service preflight passed in $($watch.Elapsed.TotalSeconds.ToString('0.000')) s."
        return
    }
    if ($pingJson.code -ne "windows_service_unavailable") {
        throw "unexpected ping preflight response: $pingResponse"
    }
    $profileText = if ([string]::IsNullOrWhiteSpace($Profile)) { "client" } else {
        Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Profile)
    }
    $connectRequest = @{
        command = "connect"
        sessionId = "preflight"
        profile = $profileText
        credentials = @{ username = "mock-user"; password = "mock-password" }
    } | ConvertTo-Json -Compress
    $writer.WriteLine($connectRequest)
    $response = $reader.ReadLine()
    $elapsed = $watch.Elapsed
    if ([string]::IsNullOrWhiteSpace($response)) { throw "daemon returned no response" }
    $json = $response | ConvertFrom-Json
    if ($json.ok -ne $false -or $json.code -ne "windows_service_unavailable") {
        throw "unexpected preflight response: $response"
    }
    if ($elapsed.TotalSeconds -ge 3) {
        throw "service preflight was not immediate: $($elapsed.TotalSeconds.ToString('0.000')) s"
    }
    Write-Output "Packaged daemon service preflight returned $($json.code) in $($elapsed.TotalSeconds.ToString('0.000')) s."
}
finally {
    if ($reader) { $reader.Dispose() }
    if ($writer) { $writer.Dispose() }
    if ($client) { $client.Dispose() }
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
