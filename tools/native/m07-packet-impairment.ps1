<# Developer-only bounded WinDivert setup, watchdog and owned-service cleanup.
The Python helper fixes its capture budget at 1,000,000 packets and records
the actual packet/queue limits in ready.json and report.json.
No SPORE launch, desktop input, firewall/routing changes or payload logging.
Use -PrepareOnly to download/verify without loading a driver. Requires PowerShell 7.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1024,65535)][int]$Port,
    [Parameter(Mandatory)][ValidateRange(0,1000)][double]$DelayMs,
    [ValidateRange(0,1000)][double]$JitterMs = 0,
    [Parameter(Mandatory)][ValidateRange(0,10)][double]$LossPercent,
    [Parameter(Mandatory)][uint32]$Seed,
    [ValidateRange(1,600)][int]$Seconds = 180,
    [Parameter(Mandatory)][string]$Output,
    [string]$Python = 'python',
    [switch]$PrepareOnly,
    [switch]$HostSelfTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$localRoot = Join-Path $repo 'local'
function Assert-LocalPath([string]$Candidate) {
    $full = [IO.Path]::GetFullPath($Candidate)
    if (-not $full.StartsWith($localRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Artifact path must stay under repository local/'
    }
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Reparse path refused'
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $full
}
function Get-DivertServices {
    @(Get-CimInstance Win32_SystemDriver -Filter "Name LIKE '%WinDivert%'" | Select-Object Name,State,StartMode,PathName)
}
function Invoke-BoundedProcess([string]$File, [string[]]$Arguments, [int]$TimeoutSeconds) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $File
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit(5000) | Out-Null
        throw 'Bounded helper process timed out'
    }
    $result = @{ exit_code=$process.ExitCode; stdout=$stdout.GetAwaiter().GetResult(); stderr=$stderr.GetAwaiter().GetResult(); pid=$process.Id }
    $process.Dispose()
    return $result
}
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 required for safe process argument handling' }
if ($JitterMs -gt $DelayMs) { throw 'Jitter must be no greater than delay' }
foreach ($number in @($DelayMs,$JitterMs,$LossPercent)) {
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) { throw 'Finite impairment values required' }
}
$outDir = Assert-LocalPath $Output
if (Test-Path -LiteralPath $outDir) { throw 'Fresh output directory required' }
$dependency = Assert-LocalPath (Join-Path $localRoot 'm07-impairment')
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
New-Item -ItemType Directory -Path $dependency -Force | Out-Null
$archive = Join-Path $dependency 'WinDivert-2.2.2-A.zip'
$package = Join-Path $dependency 'WinDivert-2.2.2-A'
$url = 'https://github.com/basil00/WinDivert/releases/download/v2.2.2/WinDivert-2.2.2-A.zip'
$archiveHash = '63CB41763BB4B20F600B6DE04E991A9C2BE73279E317D4D82F237B150C5F3F15'
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive -TimeoutSec 45 }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) { throw 'Pinned archive hash mismatch' }
# Archive is pinned before extraction; validate every destination and reject links.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        $destination = Assert-LocalPath (Join-Path $dependency $entry.FullName)
        if (-not $destination.StartsWith($package + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and $destination.TrimEnd('\','/') -ne $package) {
            throw 'Unexpected archive destination'
        }
    }
} finally { $zip.Dispose() }
Expand-Archive -LiteralPath $archive -DestinationPath $dependency -Force
$driver = Join-Path $package 'x64/WinDivert64.sys'
$dll = Join-Path $package 'x64/WinDivert.dll'
$ctl = Join-Path $package 'x64/windivertctl.exe'
$expected = @{
    $driver='8DA085332782708D8767BCACE5327A6EC7283C17CFB85E40B03CD2323A90DDC2'
    $dll='C1E060EE19444A259B2162F8AF0F3FE8C4428A1C6F694DCE20DE194AC8D7D9A2'
    $ctl='F27980B00D97E3F6A590CF4FAD04F30F4C61C72324D52AF2442AFDCF69F31765'
}
foreach ($entry in $expected.GetEnumerator()) {
    Assert-LocalPath $entry.Key | Out-Null
    if ((Get-FileHash -LiteralPath $entry.Key -Algorithm SHA256).Hash -ne $entry.Value) { throw 'Pinned dependency file mismatch' }
}
$signature = Get-AuthenticodeSignature -LiteralPath $driver
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne '043589F75FCE2795E7F2CC3E526D46784D5DDAB3') {
    throw 'Kernel driver signature is not the reviewed valid publisher signature'
}
$metadata = [ordered]@{
    schema_version=1; started_utc=[DateTime]::UtcNow.ToString('o'); archive_url=$url; archive_sha256=$archiveHash.ToLowerInvariant()
    source_tag='v2.2.2'; source_commit='1789526ecfb9ff5397c94f9f54c1a3dc2fb60440'; driver_sha256=$expected[$driver].ToLowerInvariant()
    driver_signature_status=[string]$signature.Status; signer_thumbprint=$signature.SignerCertificate.Thumbprint
    driver_load_attempted=$false; prepare_only=[bool]$PrepareOnly; port=$Port; service_cleanup_verified=$false
    desktop_input=$false; native_game_started=$false; firewall_changed=$false; routing_changed=$false
}
if ($PrepareOnly) {
    $metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outDir 'setup.json') -Encoding utf8NoBOM
    Write-Output 'Pinned package and driver signature verified; no driver loaded.'
    exit 0
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Elevated Windows process required for temporary signed driver load' }
if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
$mutex = [Threading.Mutex]::new($false,'Global\SporeMP-M07-PacketImpairment')
$locked = $false
$owned = $false
$exitCode = 1
try {
    $locked = $mutex.WaitOne(0)
    if (-not $locked) { throw 'Another M07 packet helper owns the global lease' }
    $before = @(Get-DivertServices)
    if ($before.Count) { throw 'Preexisting WinDivert service; ownership ambiguous, refusing' }
    # Require the caller to start its intended server only after ready.json.
    $sockets = @(Get-NetTCPConnection -ErrorAction SilentlyContinue | Where-Object { $_.LocalPort -eq $Port -or $_.RemotePort -eq $Port })
    if ($sockets.Count) { throw 'Dedicated test port is already in use (including TIME_WAIT)' }
    $metadata.preexisting_driver_services = 0
    $metadata.preexisting_test_port_sockets = 0
    $metadata.driver_load_attempted = $true
    $owned = $true
    $pythonPath = (Get-Command $Python -CommandType Application).Source
    $arguments = @((Join-Path $PSScriptRoot 'm07-packet-impairment.py'), '--port',[string]$Port,
        '--delay-ms',$DelayMs.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--jitter-ms',$JitterMs.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--loss-percent',$LossPercent.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--seed',[string]$Seed,'--seconds',[string]$Seconds,'--package',$package,'--output',$outDir)
    if ($HostSelfTest) { $arguments += '--host-self-test' }
    $run = Invoke-BoundedProcess $pythonPath $arguments ($Seconds + 12)
    $metadata.process = @{ pid=$run.pid; exit_code=$run.exit_code }
    $run.stdout | Set-Content -LiteralPath (Join-Path $outDir 'stdout.log') -Encoding utf8NoBOM
    $run.stderr | Set-Content -LiteralPath (Join-Path $outDir 'stderr.log') -Encoding utf8NoBOM
    $exitCode = $run.exit_code
} catch {
    $metadata.error = $_.Exception.Message
} finally {
    if ($owned) {
        try {
            $services = @(Get-DivertServices)
            foreach ($service in $services) {
                $servicePath = $service.PathName.Trim('"')
                if ($servicePath.StartsWith('\??\')) { $servicePath = $servicePath.Substring(4) }
                if ($service.Name -ne 'WinDivert' -or [IO.Path]::GetFullPath($servicePath) -ne [IO.Path]::GetFullPath($driver)) {
                    throw 'Unexpected service identity: cleanup refused'
                }
            }
            # Unlike uninstall, list never kills another process. NO_INSTALL is
            # verified in the pinned upstream source, so list cannot load a driver.
            $handles = Invoke-BoundedProcess $ctl @('list') 5
            if ($handles.exit_code -ne 0 -or $handles.stdout.Trim()) { throw 'Remaining/unknown WinDivert handles: cleanup refused' }
            if ($services.Count) {
                $stop = Invoke-BoundedProcess (Join-Path $env:SystemRoot 'System32/sc.exe') @('stop','WinDivert') 5
                $metadata.service_stop_exit_code = $stop.exit_code
                if ($stop.exit_code -notin @(0,1060,1062)) { throw 'Owned driver stop failed' }
                $delete = Invoke-BoundedProcess (Join-Path $env:SystemRoot 'System32/sc.exe') @('delete','WinDivert') 5
                $metadata.service_delete_exit_code = $delete.exit_code
                if ($delete.exit_code -notin @(0,1060)) { throw 'Owned service deletion failed' }
            }
            $remaining = @(Get-DivertServices)
            $metadata.service_cleanup_verified = ($remaining.Count -eq 0)
            if (-not $metadata.service_cleanup_verified) { throw 'Driver service still exists after cleanup' }
        } catch {
            $metadata.cleanup_error = $_.Exception.Message
            $exitCode = 1
        }
    }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    $metadata.completed_utc = [DateTime]::UtcNow.ToString('o')
    $metadata.wrapper_exit_code = $exitCode
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outDir 'setup.json') -Encoding utf8NoBOM
}
Write-Output ("Packet impairment wrapper exit {0}; owned driver cleanup verified: {1}" -f $exitCode,$metadata.service_cleanup_verified)
exit $exitCode
