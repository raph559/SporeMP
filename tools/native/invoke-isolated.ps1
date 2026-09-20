[CmdletBinding()]
param(
    [ValidateSet('Prepare','Probe','NativeProbe','Launch')][string]$Action = 'Probe',
    [string]$GameRoot = 'C:\Games\SPORE',
    [string]$RunName = ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [ValidateSet('Off','Observe','Actors','Worker')][string]$ObservationMode = 'Off',
    [ValidateSet('Game','Fullscreen','Windowed')][string]$DisplayMode = 'Game',
    [string]$Resolution = ''
)
$ErrorActionPreference = 'Stop'
# Validate before provisioning, probing, or creating a native process.
if ($DisplayMode -eq 'Game') {
    if ($Resolution) { throw 'Resolution requires Fullscreen or Windowed display mode.' }
} else {
    if ($Resolution -cnotmatch '^([1-9][0-9]{2,3})x([1-9][0-9]{2,3})$' -or
        [int]$Matches[1] -lt 640 -or [int]$Matches[1] -gt 8192 -or
        [int]$Matches[2] -lt 480 -or [int]$Matches[2] -gt 8192) {
        throw 'Resolution must be WIDTHxHEIGHT, from 640x480 through 8192x8192.'
    }
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$personalRoot = [Environment]::GetFolderPath('UserProfile')
$stateRoot = Join-Path $repoRoot 'local\native-account'
$stageRoot = 'C:\ProgramData\SporeMP\M01'
$accountName = 'SporeMP-M01'
if ($RunName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,70}$') { throw 'Invalid run name.' }
$me = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Disposable account provisioning and protected staging require an administrator.' }
$marker = Join-Path $stateRoot 'account.json'
$credentialPath = Join-Path $stateRoot 'credential.clixml'
if ($Action -eq 'Prepare' -and -not (Test-Path -LiteralPath $marker)) {
    if (Get-LocalUser -Name $accountName -ErrorAction SilentlyContinue) { throw 'Account already exists without project ownership evidence; refusing to change it.' }
    if (Test-Path -LiteralPath $stageRoot) { throw 'Unowned staging directory already exists.' }
    New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
    $passwordBytes = [Security.Cryptography.RandomNumberGenerator]::GetBytes(36)
    $passwordText = 'Spm1!' + [Convert]::ToBase64String($passwordBytes)
    $password = ConvertTo-SecureString $passwordText -AsPlainText -Force
    $passwordText = $null
    $account = New-LocalUser -Name $accountName -Password $password -Description 'SporeMP disposable M01 native qualification only' -AccountExpires (Get-Date).AddDays(14)
    Add-LocalGroupMember -SID 'S-1-5-32-545' -Member $account
    $credential = [pscredential]::new("$env:COMPUTERNAME\$accountName", $password)
    $credential | Export-Clixml -LiteralPath $credentialPath
    [ordered]@{schema_version=1;user=$accountName;sid=$account.SID.Value;personal_root=$personalRoot;game_root=[IO.Path]::GetFullPath($GameRoot);stage_root=$stageRoot;created_utc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
}
if (-not (Test-Path -LiteralPath $marker)) { throw 'Run Prepare first to provision the disposable OS account.' }
$state = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
$account = Get-LocalUser -Name $accountName
if ($account.SID.Value -ne $state.sid -or $personalRoot -ne $state.personal_root -or $GameRoot -ne $state.game_root -or $stageRoot -ne $state.stage_root) { throw 'Owned account or protected path identity changed.' }
$credential = Import-Clixml -LiteralPath $credentialPath
function Set-OwnedAcl([string]$Path, [Security.AccessControl.FileSystemRights]$Rights) {
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true,$false)
    foreach ($sid in @('S-1-5-18','S-1-5-32-544')) { $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','ContainerInherit,ObjectInherit','None','Allow')) }
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($account.SID,$Rights,'ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $Path -AclObject $acl
}
Set-OwnedAcl $stateRoot ([Security.AccessControl.FileSystemRights]::ReadAndExecute)
# Credentials remain readable only by the operator, administrators and SYSTEM.
$secretAcl = Get-Acl -LiteralPath $credentialPath
$secretAcl.SetAccessRuleProtection($true,$false)
foreach ($sid in @('S-1-5-18','S-1-5-32-544')) { $secretAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','Allow')) }
Set-Acl -LiteralPath $credentialPath -AclObject $secretAcl
Set-OwnedAcl $stageRoot ([Security.AccessControl.FileSystemRights]::ReadAndExecute)
$gameAcl = Get-Acl -LiteralPath $GameRoot
$aclBackup = Join-Path $stateRoot 'game-original-acl.txt'
if (-not (Test-Path -LiteralPath $aclBackup)) { $gameAcl.Sddl | Set-Content -LiteralPath $aclBackup }
$denyRights = [Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership
$deny = [Security.AccessControl.FileSystemAccessRule]::new($account.SID,$denyRights,'ContainerInherit,ObjectInherit','None','Deny')
$gameAcl.SetAccessRule($deny)
Set-Acl -LiteralPath $GameRoot -AclObject $gameAcl
$runRoot = Join-Path $stageRoot "runs\$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Run directory exists; use a fresh name.' }
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
Set-OwnedAcl $runRoot ([Security.AccessControl.FileSystemRights]::Modify)
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'profile-probe.ps1') -Destination (Join-Path $stageRoot 'profile-probe.ps1') -Force
$info = [Diagnostics.ProcessStartInfo]::new((Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'))
$info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.LoadUserProfile=$true
$info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
$info.UserName=$accountName; $info.Domain=$env:COMPUTERNAME; $info.Password=$credential.Password; $info.WorkingDirectory=$stageRoot
foreach ($arg in @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',(Join-Path $stageRoot 'profile-probe.ps1'),'-Output',(Join-Path $runRoot 'os-probe.json'),'-PersonalRoot',$personalRoot,'-GameRoot',$GameRoot)) { $info.ArgumentList.Add($arg) }
$probe = [Diagnostics.Process]::Start($info)
$probeOutput = $probe.StandardOutput.ReadToEnd()
$probeError = $probe.StandardError.ReadToEnd()
$probe.WaitForExit()
$probeOutput | Set-Content -LiteralPath (Join-Path $runRoot 'probe-stdout.txt')
$probeError | Set-Content -LiteralPath (Join-Path $runRoot 'probe-stderr.txt')
if ($probe.ExitCode -ne 0) { throw "OS profile probe failed with exit $($probe.ExitCode); see $runRoot" }
$report = Get-Content -LiteralPath (Join-Path $runRoot 'os-probe.json') -Raw | ConvertFrom-Json
if (-not $report.os_isolation_gate -or $report.sid -ne $state.sid) { throw 'Child isolation report does not match the owned account.' }
Write-Host "OS profile probe passed: $($report.user), $($report.shell_folders.UserProfile)"
Write-Host "Evidence: $runRoot"
if ($Action -notin @('NativeProbe','Launch')) { exit 0 }
if (Get-Process -Name SporeApp -ErrorAction SilentlyContinue) { throw 'A game is already running; stop it normally before qualification.' }
$nativeBinary = Join-Path $repoRoot "build\win32\$Configuration\SporeMP.NativeHost.exe"
$nativeHash = (Get-FileHash -LiteralPath $nativeBinary -Algorithm SHA256).Hash.ToLowerInvariant()
$payload = Join-Path $stageRoot "payload\$nativeHash"
if (-not (Test-Path -LiteralPath $payload)) {
    New-Item -ItemType Directory -Path (Join-Path $payload 'mLibs') -Force | Out-Null
    Set-OwnedAcl $payload ([Security.AccessControl.FileSystemRights]::ReadAndExecute)
    $files = @(
        @($nativeBinary,'SporeMP.NativeHost.exe'),
        @((Join-Path $repoRoot "build\injector\$Configuration\ModAPI.DLLInjector.dll"),'ModAPI.DLLInjector.dll'),
        @((Join-Path $repoRoot "build\sdk\$Configuration\SporeModAPI.dll"),'mLibs\SporeModAPI.dll'),
        @((Join-Path $repoRoot "build\win32\$Configuration\SporeMP.Bridge.dll"),'mLibs\SporeMP.Bridge.dll')
    )
    foreach ($pair in $files) {
        Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $payload $pair[1])
        if ((Get-FileHash -LiteralPath $pair[0]).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $pair[1])).Hash) { throw 'Staged artifact hash mismatch.' }
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $payload 'SporeMP.NativeHost.exe')).Hash.ToLowerInvariant() -ne $nativeHash) { throw 'Staged host identity changed.' }
if ($Action -eq 'Launch') {
    # Reverify and reuse an unchanged backup before every native attempt.
    & python (Join-Path $PSScriptRoot 'personal-files.py') before --report (Join-Path $runRoot 'personal-backup.json') --output (Join-Path $runRoot 'personal-backup.json')
    if ($LASTEXITCODE -ne 0) { throw 'Personal backup failed; native launch denied.' }
}
$nativeInfo = [Diagnostics.ProcessStartInfo]::new((Join-Path $payload 'SporeMP.NativeHost.exe'))
$nativeInfo.UseShellExecute=$false; $nativeInfo.CreateNoWindow=$true; $nativeInfo.LoadUserProfile=$true
$nativeInfo.UserName=$accountName; $nativeInfo.Domain=$env:COMPUTERNAME; $nativeInfo.Password=$credential.Password; $nativeInfo.WorkingDirectory=$payload
$mode = if ($Action -eq 'Launch' -and $ObservationMode -eq 'Worker') { '--worker' } elseif ($Action -eq 'Launch' -and $ObservationMode -eq 'Actors') { '--actors' } elseif ($Action -eq 'Launch' -and $ObservationMode -eq 'Observe') { '--observe' } elseif ($Action -eq 'Launch') { '--launch' } else { '--probe' }
foreach ($arg in @($mode,$GameRoot,$payload,$runRoot,$state.sid,$personalRoot)) { $nativeInfo.ArgumentList.Add($arg) }
if ($mode -eq '--worker') {
    $nativeInfo.ArgumentList.Add([guid]::NewGuid().ToString('N'))
    $nativeInfo.ArgumentList.Add('private')
}
if ($DisplayMode -ne 'Game') {
    foreach ($arg in @('--display-mode',$DisplayMode.ToLowerInvariant(),'--resolution',$Resolution)) { $nativeInfo.ArgumentList.Add($arg) }
}
$nativeProcess = [Diagnostics.Process]::Start($nativeInfo)
Write-Host "Native host PID: $($nativeProcess.Id); mode: $mode"
$nativeProcess.WaitForExit()
Write-Host "Native host exit: $($nativeProcess.ExitCode)"
if ($Action -eq 'Launch') {
    & python (Join-Path $PSScriptRoot 'personal-files.py') after --report (Join-Path $runRoot 'personal-backup.json') --output (Join-Path $runRoot 'personal-after.json')
    if ($LASTEXITCODE -ne 0) { throw 'Post-run personal file verification failed; retain all evidence.' }
    Write-Host 'Personal source hashes unchanged after native exit.'
}
exit $nativeProcess.ExitCode
