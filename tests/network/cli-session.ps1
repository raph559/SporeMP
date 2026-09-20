param(
    [Parameter(Mandatory=$true)][string]$Coordinator,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $Coordinator).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Output must be a fresh directory.' }
$null = New-Item -ItemType Directory -Path $outputPath
$serverConfig = Join-Path $outputPath 'server.conf'
$privatePath = Join-Path $outputPath 'private-session'
$identityLines = @('schema=1','host=127.0.0.1','port=0',('build_sha256=' + ('11' * 32)),('executable_sha256=' + ('22' * 32)),('content_sha256=' + ('33' * 32)),('fixture_sha256=' + ('44' * 32)))
[IO.File]::WriteAllText($serverConfig, ($identityLines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
function Start-TestProcess([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $binary
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'Process did not start.' }
    return $process
}
function Complete-Probe($Process) {
    if (-not $Process.WaitForExit(15000)) { $Process.Kill(); throw 'Probe exceeded its bounded deadline.' }
    $stdout = $Process.StandardOutput.ReadToEnd()
    $stderr = $Process.StandardError.ReadToEnd()
    return @{pid=$Process.Id;exit_code=$Process.ExitCode;stdout=$stdout;stderr=$stderr;result=($stdout | ConvertFrom-Json)}
}
$server = $null
$report = [ordered]@{classification='HOST actual separate processes and Schannel TCP; no game launched';binary=$binary;binary_sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant();commands=@();checks=@()}
try {
    $serverArguments = @('--serve','--config',$serverConfig,'--output',$privatePath)
    $report.commands += ,(@($binary) + $serverArguments)
    $server = Start-TestProcess $serverArguments
    $line = $server.StandardOutput.ReadLineAsync()
    if (-not $line.Wait(10000)) { throw 'Coordinator did not report readiness.' }
    $ready = $line.Result | ConvertFrom-Json
    if (-not $ready.ready) { throw 'Coordinator rejected the host configuration.' }
    $report.server_pid = $server.Id
    $report.server_ready = $ready
    $one = Join-Path $privatePath 'player-1.conf'
    $two = Join-Path $privatePath 'player-2.conf'
    $oneArguments = @('--client-probe','--config',$one)
    $twoArguments = @('--client-probe','--config',$two)
    $report.commands += ,(@($binary) + $oneArguments)
    $report.commands += ,(@($binary) + $twoArguments)
    $first = Start-TestProcess $oneArguments
    $second = Start-TestProcess $twoArguments
    $firstResult = Complete-Probe $first
    $secondResult = Complete-Probe $second
    if ($firstResult.exit_code -ne 0 -or $secondResult.exit_code -ne 0 -or -not $firstResult.result.authenticated -or -not $secondResult.result.authenticated) { throw 'Real peer authentication failed.' }
    if ($firstResult.result.player -ne 1 -or $secondResult.result.player -ne 2 -or $firstResult.result.session -ne $secondResult.result.session) { throw 'Players were not distinct in the same session.' }
    $report.first = $firstResult
    $report.second = $secondResult
    $report.checks += 'Two separate probe process IDs authenticated as different players in one live coordinator session.'
    $reconnectResult = Complete-Probe (Start-TestProcess $oneArguments)
    if ($reconnectResult.exit_code -ne 0 -or $reconnectResult.result.player -ne 1 -or $reconnectResult.result.session -ne $firstResult.result.session) { throw 'Stable reconnect identity failed.' }
    $report.reconnect = $reconnectResult
    $report.checks += 'A third probe process reused the same credential and retained player identity 1.'
    $badConfig = Join-Path $privatePath 'mismatched-build.conf'
    $badText = [IO.File]::ReadAllText($one).Replace(('build_sha256=' + ('11' * 32)), ('build_sha256=' + ('55' * 32)))
    [IO.File]::WriteAllText($badConfig,$badText,[Text.UTF8Encoding]::new($false))
    $badArguments = @('--client-probe','--config',$badConfig)
    $report.commands += ,(@($binary) + $badArguments)
    $badResult = Complete-Probe (Start-TestProcess $badArguments)
    if ($badResult.exit_code -ne 20 -or $badResult.result.error -ne 'incompatible_build_executable_or_content') { throw 'Actual CLI mismatch rejection failed.' }
    $report.mismatch = $badResult
    $report.checks += 'A changed native build digest was explicitly rejected over TLS with exit 20.'
    foreach ($invite in @('player-1.invite','player-2.invite')) {
        if (-not [IO.File]::ReadAllText((Join-Path $privatePath $invite)).StartsWith('sporemp://join?host=127.0.0.1&port=')) { throw 'Private invitation file missing.' }
    }
    $acl = Get-Acl -LiteralPath $privatePath
    if (-not $acl.AreAccessRulesProtected) { throw 'Private credential directory inherited an unrestricted ACL.' }
    $report.private_directory_acl = $acl.Sddl
    $report.checks += 'Invitation files exist inside a directory with a protected explicit ACL.'
    $logPath = Join-Path $privatePath 'coordinator.jsonl'
    $flushDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while ([DateTime]::UtcNow -lt $flushDeadline -and (Get-Item -LiteralPath $logPath).Length -eq 0) { Start-Sleep -Milliseconds 20 }
    $report.log_bytes_before_stop = (Get-Item -LiteralPath $logPath).Length
    if ($report.log_bytes_before_stop -eq 0) { throw 'Low-volume coordinator events were not flushed while the process was alive.' }
    $report.checks += 'Low-volume coordinator event bytes are readable before shutdown, proving periodic flush below the 128-event batch threshold.'
    [IO.File]::WriteAllText((Join-Path $privatePath 'stop.request'),'')
    if (-not $server.WaitForExit(10000)) { throw 'Coordinator clean stop timed out.' }
    $report.server_exit_code = $server.ExitCode
    $report.server_stderr = $server.StandardError.ReadToEnd()
    if ($server.ExitCode -ne 0) { throw 'Coordinator failed clean shutdown.' }
    $report.checks += 'stop.request drained and closed the coordinator with exit 0.'
    $report.status = 'PASS'
} finally {
    if ($server -and -not $server.HasExited) {
        if (Test-Path -LiteralPath $privatePath) { [IO.File]::WriteAllText((Join-Path $privatePath 'stop.request'),'') }
        if (-not $server.WaitForExit(10000)) { $server.Kill(); $server.WaitForExit(); $report.forced_cleanup = $true }
    }
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $outputPath 'result.json') -Encoding utf8NoBOM
}
Write-Output ($report | ConvertTo-Json -Depth 10)
