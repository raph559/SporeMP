[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$GamePid,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(5,600)][int]$Seconds = 180,
    [switch]$AllowConcurrent
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$localRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'local')) + [IO.Path]::DirectorySeparatorChar
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory, $repoRoot)
if (-not $outputRoot.StartsWith($localRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Raw captures must stay in the ignored local directory.' }
if (Test-Path -LiteralPath $outputRoot) { throw 'Use a fresh capture output directory.' }
# A PID is required as well as exact window/executable filters. Never fall back to
# a desktop, monitor, unrelated app, or one of several simultaneous game processes.
$games = @(Get-CimInstance Win32_Process -Filter "Name = 'SporeApp.exe'")
$selectedGames = @($games | Where-Object ProcessId -eq $GamePid)
if ((-not $AllowConcurrent -and $games.Count -ne 1) -or $selectedGames.Count -ne 1 -or $selectedGames[0].ExecutablePath -ne 'C:\Games\SPORE\SporebinEP1\SporeApp.exe') {
    throw 'Exactly one matching original SPORE process is required. No capture started.'
}
$selectedGame = $selectedGames[0]
$captureProcess = Get-Process -Id $GamePid -ErrorAction Stop
$captureHandle = $captureProcess.MainWindowHandle.ToInt64()
if ($captureHandle -le 0 -or $captureProcess.MainWindowTitle -cne 'SPORE™ Galactic Adventures' -or $captureProcess.Path -ne $selectedGame.ExecutablePath) {
    throw 'The exact process has no matching original game window. No capture started.'
}
$parent = [IO.DirectoryInfo]::new($outputRoot)
while ($parent) {
    if ($parent.Exists -and ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Capture path contains a reparse point.' }
    $parent = $parent.Parent
}
$ffmpeg = (Get-Command ffmpeg -CommandType Application).Source
$ffmpegItem = Get-Item -LiteralPath $ffmpeg
if ($ffmpegItem.LinkType) { $ffmpeg = $ffmpegItem.ResolveLinkTarget($true).FullName }
$help = & $ffmpeg -hide_banner -h filter=gfxcapture 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'gfxcapture AVOptions:' -or $help -notmatch 'hwnd\s+<uint64>') { throw 'Installed FFmpeg lacks exact HWND capture.' }
New-Item -ItemType Directory -Path $outputRoot | Out-Null
$filter = "gfxcapture=hwnd=${captureHandle}:capture_cursor=0:max_framerate=30,hwdownload,format=bgra,format=yuv420p"
# Flush short Matroska clusters so a live first-frame reader sees complete data.
# These container options leave the encoder, quality and captured frames unchanged.
$arguments = @('-hide_banner','-loglevel','info','-nostats','-stats_period','1','-progress',(Join-Path $outputRoot 'progress.txt'),'-filter_complex',$filter,'-t',"$Seconds",'-an','-c:v','libx264','-preset','ultrafast','-crf','23','-fps_mode','vfr','-flush_packets','1','-cluster_time_limit','1000','-n',(Join-Path $outputRoot 'capture.mkv'))
$request = [ordered]@{schema_version=1;utc=(Get-Date).ToUniversalTime().ToString('o');game_pid=$GamePid;game_executable=$selectedGame.ExecutablePath;game_created=$selectedGame.CreationDate;window_handle=$captureHandle;window_title=$captureProcess.MainWindowTitle;concurrent_capture=[bool]$AllowConcurrent;recorder_executable=$ffmpeg;recorder_sha256=(Get-FileHash -LiteralPath $ffmpeg -Algorithm SHA256).Hash.ToLowerInvariant();arguments=$arguments;maximum_seconds=$Seconds;source='Windows.Graphics.Capture via FFmpeg gfxcapture';visual_review='REQUIRED: frame count and dimensions do not establish captured game content';native_frame_timing='NOT_MEASURED: compositor capture cadence is not game timing';assistant_desktop_input=$false}
$request | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'request.json') -Encoding utf8NoBOM
$help | Set-Content -LiteralPath (Join-Path $outputRoot 'gfxcapture-help.txt') -Encoding utf8NoBOM
$startInfo = [Diagnostics.ProcessStartInfo]::new($ffmpeg)
$startInfo.UseShellExecute=$false; $startInfo.CreateNoWindow=$true
$startInfo.RedirectStandardOutput=$true; $startInfo.RedirectStandardError=$true
foreach ($argument in $arguments) { $startInfo.ArgumentList.Add($argument) }
$stdout = [IO.File]::Open((Join-Path $outputRoot 'stdout.txt'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$stderr = [IO.File]::Open((Join-Path $outputRoot 'stderr.txt'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$process = $null
$timedOut = $false
try {
    $process = [Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
    Write-Output "WGC recorder PID $($process.Id). Inspect actual captured game frames before giving gameplay instructions."
    # Wall-clock bound also covers a minimized/non-rendering source that never emits frames.
    if (-not $process.WaitForExit(($Seconds + 15) * 1000)) {
        $timedOut = $true
        $process.Kill() # Only this owned recorder; never the game or another application.
        $process.WaitForExit()
    }
    $stdoutTask.GetAwaiter().GetResult()
    $stderrTask.GetAwaiter().GetResult()
    [ordered]@{schema_version=1;utc=(Get-Date).ToUniversalTime().ToString('o');recorder_pid=$process.Id;exit_code=$process.ExitCode;wall_timeout=$timedOut;visual_acceptance='NOT_VERIFIED: inspect previews throughout the clip, including transitions';native_frame_timing='NOT_MEASURED'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'completion.json') -Encoding utf8NoBOM
    if ($timedOut -or $process.ExitCode -ne 0) { throw 'Capture did not finish normally; retain diagnostics and review partial media separately.' }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $stdout.Dispose()
    $stderr.Dispose()
    if ($process) { $process.Dispose() }
}
