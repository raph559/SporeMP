param(
    [Parameter(Mandatory)][int]$GamePid,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]{1,35}$')][string]$RunKey,
    [ValidateRange(5,120)][int]$Seconds = 60,
    [string]$OutputRoot = ""
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$target=Get-CimInstance Win32_Process -Filter "ProcessId = $GamePid"
if ($target.Name -ne 'SporeApp.exe' -or $target.ExecutablePath -ne 'C:\Games\SPORE\SporebinEP1\SporeApp.exe') { throw 'Wrong native target.' }
$tool=Join-Path $repoRoot 'external/PresentMon-2.5.1/PresentMon-2.5.1-x64.exe'
$hash=(Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne '9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191') { throw 'Unpinned tool.' }
if (-not $OutputRoot) { $OutputRoot=Join-Path $repoRoot 'local/native-timing' }
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) $RunKey
if (Test-Path -LiteralPath $root) { throw 'Use a fresh timing directory.' }
New-Item -ItemType Directory -Path $root | Out-Null
$csv=Join-Path $root 'frames.csv'
if (Test-Path -LiteralPath $csv) { throw 'Do not overwrite frame evidence.' }
$arguments=@('--process_id',"$GamePid",'--output_file',$csv,'--session_name',('SporeMP-Timing-'+$RunKey),'--timed',"$Seconds",'--terminate_after_timed','--no_track_input','--no_console_stats','--v1_metrics')
$before=Get-Process -Id $GamePid
$cpuBefore=$before.TotalProcessorTime.TotalSeconds
$start=(Get-Date).ToUniversalTime()
& $tool @arguments > (Join-Path $root 'frames-output.txt') 2>&1
$result=$LASTEXITCODE
$finish=(Get-Date).ToUniversalTime()
$after=Get-Process -Id $GamePid
[ordered]@{schema_version=1;game_pid=$GamePid;game_created=$target.CreationDate;command=@($tool)+$arguments;tool_sha256=$hash;started_utc=$start.ToString('o');ended_utc=$finish.ToString('o');expected_exit=0;observed_exit=$result;wall_seconds=($finish-$start).TotalSeconds;process_cpu_seconds=$after.TotalProcessorTime.TotalSeconds-$cpuBefore;working_set_bytes=$after.WorkingSet64;note='Actual native D3D9 presentation intervals. Not simulation ticks. Process CPU is a whole-window measure; desktop/recorder and uncontrolled NPC timing are possible confounders.'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'frames-request.json') -Encoding utf8NoBOM
Write-Output "PresentMon completed with exit $result for $RunKey."
exit $result
