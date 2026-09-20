[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('01','02','03')][string]$WorkerId,
      [string]$Fixture='local/m03-fixtures/0912-post-award01')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$stateRoot=Join-Path $repoRoot "local/worker-accounts/$WorkerId"
$stageRoot="C:\ProgramData\SporeMP\M04\$WorkerId"
$accountName="SporeMP-M04-$WorkerId"
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Preparing an isolated worker requires an administrator.'}
if(Get-Process -Name SporeApp -ErrorAction SilentlyContinue){throw 'Close SPORE before preparing a fixture account.'}
$marker=Join-Path $stateRoot 'account.json'
if(Test-Path -LiteralPath $marker){throw 'This worker already exists; its profile and fixture will not be overwritten.'}
if((Get-LocalUser -Name $accountName -ErrorAction SilentlyContinue) -or (Test-Path -LiteralPath $stageRoot) -or (Test-Path -LiteralPath "C:\Users\$accountName")) {
    throw 'An account or workspace exists without this worker ownership record.'
}
$fixturePath=[IO.Path]::GetFullPath((Join-Path $repoRoot $Fixture))
$fixturePrefix=[IO.Path]::GetFullPath((Join-Path $repoRoot 'local/m03-fixtures'))+'\'
if(-not $fixturePath.StartsWith($fixturePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Only a preserved local M03 fixture may seed this developer worker.'}
& python (Join-Path $repoRoot 'tools/diagnostics/sporemp_diag.py') verify-backup --destination $fixturePath | Out-Null
if($LASTEXITCODE -ne 0){throw 'Fixture backup verification failed.'}
$fixtureManifest=Get-Content -LiteralPath (Join-Path $fixturePath 'backup-manifest.json') -Raw | ConvertFrom-Json
if($fixtureManifest.sources.Count -ne 2){throw 'Expected the closed Spore and creation fixture trees.'}
New-Item -ItemType Directory -Path $stateRoot,$stageRoot -Force | Out-Null
function Protect-Directory([string]$Path,[string]$WorkerSid,[Security.AccessControl.FileSystemRights]$Rights){
    $acl=[Security.AccessControl.DirectorySecurity]::new();$acl.SetAccessRuleProtection($true,$false)
    foreach($sid in @('S-1-5-18','S-1-5-32-544')){$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','ContainerInherit,ObjectInherit','None','Allow'))}
    if($WorkerSid){$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($WorkerSid),$Rights,'ContainerInherit,ObjectInherit','None','Allow'))}
    Set-Acl -LiteralPath $Path -AclObject $acl
}
Protect-Directory $stateRoot '' 'ReadAndExecute'
$password=ConvertTo-SecureString ('Spm4!'+[Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(36))) -AsPlainText -Force
$account=New-LocalUser -Name $accountName -Password $password -Description 'SporeMP M04 isolated native worker qualification' -AccountExpires (Get-Date).AddDays(14)
Add-LocalGroupMember -SID 'S-1-5-32-545' -Member $account
$credential=[pscredential]::new("$env:COMPUTERNAME\$accountName",$password)
$credential | Export-Clixml -LiteralPath (Join-Path $stateRoot 'credential.clixml')
Protect-Directory $stageRoot $account.SID.Value 'ReadAndExecute'
$gameRoot='C:\Games\SPORE'
$gameAcl=Get-Acl -LiteralPath $gameRoot
$gameAcl.Sddl | Set-Content -LiteralPath (Join-Path $stateRoot 'game-original-acl.txt')
$denyRights=[Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership
$gameAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($account.SID,$denyRights,'ContainerInherit,ObjectInherit','None','Deny'))
Set-Acl -LiteralPath $gameRoot -AclObject $gameAcl
$record=[ordered]@{schema_version=1;worker_id=$WorkerId;user=$accountName;sid=$account.SID.Value;personal_root=[Environment]::GetFolderPath('UserProfile');game_root=$gameRoot;stage_root=$stageRoot;profile="C:\Users\$accountName";fixture=$fixturePath;fixture_sha256=(Get-FileHash -LiteralPath (Join-Path $fixturePath 'backup-manifest.json')).Hash.ToLowerInvariant();prepared=$false;created_utc=(Get-Date).ToUniversalTime().ToString('o')}
$record | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8NoBOM
$probeRoot=Join-Path $stageRoot 'provision'
New-Item -ItemType Directory -Path $probeRoot | Out-Null
Protect-Directory $probeRoot $account.SID.Value 'Modify'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'profile-probe.ps1') -Destination (Join-Path $stageRoot 'profile-probe.ps1')
$info=[Diagnostics.ProcessStartInfo]::new("$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe")
$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.LoadUserProfile=$true
$info.UserName=$accountName;$info.Domain=$env:COMPUTERNAME;$info.Password=$credential.Password;$info.WorkingDirectory=$stageRoot
foreach($arg in @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',(Join-Path $stageRoot 'profile-probe.ps1'),'-Output',(Join-Path $probeRoot 'os-probe.json'),'-PersonalRoot',$record.personal_root,'-GameRoot',$gameRoot,'-ExpectedUser',$accountName)){$info.ArgumentList.Add($arg)}
$probe=[Diagnostics.Process]::Start($info);$probe.WaitForExit()
if($probe.ExitCode -ne 0){throw 'Worker profile protection probe failed; partial setup is preserved.'}
$report=Get-Content -LiteralPath (Join-Path $probeRoot 'os-probe.json') -Raw | ConvertFrom-Json
if($report.sid -ne $record.sid -or $report.shell_folders.UserProfile -ne $record.profile -or -not $report.os_isolation_gate){throw 'Worker profile identity mismatch.'}
$targets=@((Join-Path $record.profile 'AppData/Roaming/Spore'),(Join-Path $record.profile 'Documents/My Spore Creations'))
for($i=0;$i -lt 2;$i++){
    if(Test-Path -LiteralPath $targets[$i]){throw 'Refusing to overwrite a pre-existing worker save/creation tree.'}
    New-Item -ItemType Directory -Path $targets[$i] | Out-Null
    Protect-Directory $targets[$i] $account.SID.Value 'Modify'
    Get-ChildItem -LiteralPath (Join-Path $fixturePath "source-$i") -Force | Copy-Item -Destination $targets[$i] -Recurse
    foreach($file in $fixtureManifest.sources[$i].snapshot.files){
        $copied=Join-Path $targets[$i] $file.path
        if((Get-FileHash -LiteralPath $copied).Hash.ToLowerInvariant() -ne $file.sha256){throw 'Seeded fixture hash mismatch.'}
    }
}
$record.prepared=$true
$record | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8NoBOM
[ordered]@{worker_id=$WorkerId;prepared=$true;profile=$record.profile;native_isolation='NOT_RUN'} | ConvertTo-Json -Compress
