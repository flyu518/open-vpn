param(
    [string]$LauncherPath = "tmp/package-real3/openvpn_windows_daemon_launcher.exe"
)

$ErrorActionPreference = "Stop"
$launcherPath = (Resolve-Path -LiteralPath $LauncherPath).Path
$workDir = Split-Path -Parent $launcherPath
$before = @(Get-Process -Name openvpn_windows_daemon -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$launcherProcess = $null
$daemon = $null
$client = $null
$writer = $null
$reader = $null
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $launcherPath
    $startInfo.WorkingDirectory = $workDir
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $launcherProcess = [Diagnostics.Process]::new()
    $launcherProcess.StartInfo = $startInfo
    [void]$launcherProcess.Start()
    if (-not $launcherProcess.WaitForExit(5000) -or $launcherProcess.ExitCode -ne 0) {
        throw "hidden daemon launcher failed with exit code $($launcherProcess.ExitCode)"
    }

    for ($attempt = 0; $attempt -lt 20 -and -not $daemon; $attempt++) {
        $daemon = @(Get-Process -Name openvpn_windows_daemon -ErrorAction SilentlyContinue |
            Where-Object { $before -notcontains $_.Id } | Select-Object -First 1)
        if (-not $daemon) { Start-Sleep -Milliseconds 100 }
    }
    if (-not $daemon) { throw "hidden launcher did not start the daemon" }
    $daemon = $daemon[0]

    $connected = $false
    for ($attempt = 0; $attempt -lt 20 -and -not $connected; $attempt++) {
        $candidate = [IO.Pipes.NamedPipeClientStream]::new(".", "openvpn-ui", [IO.Pipes.PipeDirection]::InOut)
        try {
            $candidate.Connect(500)
            $client = $candidate
            $connected = $true
        } catch {
            $candidate.Dispose()
            if ($daemon.HasExited) { throw "packaged daemon exited with code $($daemon.ExitCode)" }
        }
    }
    if (-not $connected) { throw "hidden launcher did not expose the daemon pipe" }
    $writer = [IO.StreamWriter]::new($client, [Text.UTF8Encoding]::new($false), 8192, $true)
    $writer.AutoFlush = $true
    $reader = [IO.StreamReader]::new($client, [Text.UTF8Encoding]::new($false), $false, 8192, $true)
    $writer.WriteLine('{"command":"ping"}')
    $response = $reader.ReadLine()
    $json = $response | ConvertFrom-Json
    if ($json.ok -ne $false -or $json.code -ne "windows_service_unavailable") {
        throw "unexpected hidden daemon response: $response"
    }
    Write-Output "Hidden daemon launcher started the packaged daemon; ping returned $($json.code)."
}
finally {
    if ($reader) { $reader.Dispose() }
    if ($writer) { $writer.Dispose() }
    if ($client) { $client.Dispose() }
    if ($daemon -and -not $daemon.HasExited) { Stop-Process -Id $daemon.Id -Force }
}
