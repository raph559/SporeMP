[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('01','02','03')][string]$WorkerId,
      [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{32}$')][string]$Generation,
      [Parameter(Mandatory)][ValidateSet('private','current')][string]$Desktop,
      [ValidateSet('Authority','Replica')][string]$M05Role,
      [string]$M06SessionConfig)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$stateRoot=Join-Path $repoRoot "local/worker-accounts/$WorkerId"
function Assert-LocalPath([string]$Path){
    $at=[IO.Path]::GetFullPath($Path)
    while($at){
        if((Test-Path -LiteralPath $at) -and ((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Worker reparse path refused.'}
        $at=[IO.Path]::GetDirectoryName($at)
    }
}
function Start-DetachedWorkerSupervisor([string]$Executable,[string]$WorkingDirectory,[string[]]$Arguments,
                                        [pscredential]$Credential,[string]$RunDirectory){
    $inputPath=Join-Path $RunDirectory 'supervisor-stdin.txt'
    $outputPath=Join-Path $RunDirectory 'supervisor-stdout.log'
    $errorPath=Join-Path $RunDirectory 'supervisor-stderr.log'
    foreach($path in @($inputPath,$outputPath,$errorPath)){Assert-LocalPath $path}
    [IO.File]::Open($inputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read).Dispose()
    # Start-Process joins ArgumentList before calling Windows. Quote each argument,
    # doubling backslashes before quotes and the closing quote to retain its value.
    $argumentLine=($Arguments | ForEach-Object {
        '"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'
    }) -join ' '
    # On Windows these redirections supply native file handles to STARTUPINFO.
    # A long-lived worker must not keep the launcher's captured pipes open after
    # this script and the Python backend exit. Redirect stdin as well as both logs.
    # With credentials, UseNewEnvironment keeps the environment pointer null so
    # Windows builds it for the worker account instead of copying this user's.
    Start-Process -FilePath $Executable -ArgumentList $argumentLine -WorkingDirectory $WorkingDirectory `
        -Credential $Credential -LoadUserProfile -UseNewEnvironment -WindowStyle Hidden -PassThru `
        -RedirectStandardInput $inputPath -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath
}
Assert-LocalPath $stateRoot
foreach($name in @('account.json','credential.clixml','current.json','start.lock')){Assert-LocalPath (Join-Path $stateRoot $name)}
$launchLock=[IO.File]::Open((Join-Path $stateRoot 'start.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
$state=Get-Content -LiteralPath (Join-Path $stateRoot 'account.json') -Raw | ConvertFrom-Json
$user=Get-LocalUser -Name "SporeMP-M04-$WorkerId"
if(-not $state.prepared -or $user.SID.Value -ne $state.sid -or $state.worker_id -ne $WorkerId -or
   $state.user -ne $user.Name -or $state.stage_root -ne "C:\ProgramData\SporeMP\M04\$WorkerId" -or
   $state.profile -ne "C:\Users\$($user.Name)" -or $state.personal_root -ne [Environment]::GetFolderPath('UserProfile') -or $state.game_root -ne 'C:\Games\SPORE') {throw 'Worker account ownership changed.'}
$previousPath=Join-Path $stateRoot 'current.json'
if(Test-Path -LiteralPath $previousPath){
    $previous=Get-Content -LiteralPath $previousPath -Raw | ConvertFrom-Json
    $previousProcess=Get-Process -Id $previous.supervisor_pid -ErrorAction SilentlyContinue
    if($previousProcess -and $previousProcess.Path -eq (Join-Path $previous.payload 'SporeMP.NativeHost.exe')){throw 'This worker supervisor is already running.'}
}
$credential=Import-Clixml -LiteralPath (Join-Path $stateRoot 'credential.clixml')
$native=Join-Path $repoRoot 'build/win32/Release/SporeMP.NativeHost.exe'
$hash=(Get-FileHash -LiteralPath $native).Hash.ToLowerInvariant()
$payload=Join-Path $state.stage_root "payload/$hash"
$run=Join-Path $state.stage_root "runs/$Generation"
if(Test-Path -LiteralPath $run){throw 'Worker run already exists.'}
Assert-LocalPath $stateRoot;Assert-LocalPath $payload;Assert-LocalPath $run;Assert-LocalPath $state.profile
if(-not(Test-Path -LiteralPath $payload)){
    New-Item -ItemType Directory -Path (Join-Path $payload 'mLibs') -Force | Out-Null
    foreach($pair in @(@($native,'SporeMP.NativeHost.exe'),@((Join-Path $repoRoot 'build/injector/Release/ModAPI.DLLInjector.dll'),'ModAPI.DLLInjector.dll'),@((Join-Path $repoRoot 'build/sdk/Release/SporeModAPI.dll'),'mLibs/SporeModAPI.dll'),@((Join-Path $repoRoot 'build/win32/Release/SporeMP.Bridge.dll'),'mLibs/SporeMP.Bridge.dll'))){
        Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $payload $pair[1])
        if((Get-FileHash -LiteralPath $pair[0]).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $pair[1])).Hash){throw 'Worker payload copy mismatch.'}
    }
}
if((Get-FileHash -LiteralPath (Join-Path $payload 'SporeMP.NativeHost.exe')).Hash.ToLowerInvariant() -ne $hash){throw 'Staged supervisor changed.'}
New-Item -ItemType Directory -Path $run -Force | Out-Null
$acl=Get-Acl -LiteralPath $run
$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($user.SID,'Modify','ContainerInherit,ObjectInherit','None','Allow'))
Set-Acl -LiteralPath $run -AclObject $acl
& python (Join-Path $PSScriptRoot 'worker-personal.py') before --output (Join-Path $run 'personal-before.json') | Out-Null
if($LASTEXITCODE -ne 0){throw 'Worker personal-file protection check failed; no game launched.'}
$workerCredential=[pscredential]::new("$env:COMPUTERNAME\$($user.Name)",$credential.Password)
$hostMode=if($M05Role -eq 'Authority'){'--authority-probe'}elseif($M05Role -eq 'Replica'){'--replica-probe'}else{'--worker'}
if($M06SessionConfig){
    if(-not $M05Role){throw 'Network worker requires an explicit authority/replica role.'}
    $sessionSource=[IO.Path]::GetFullPath($M06SessionConfig)
    Assert-LocalPath $sessionSource
    $sessionTarget=Join-Path $run 'network-session.cfg'
    Copy-Item -LiteralPath $sessionSource -Destination $sessionTarget
    # The run ACL admits only its disposable account and developer administrators.
    $hostMode=if($M05Role -eq 'Authority'){'--network-authority'}else{'--network-replica'}
}
$workerArguments=@($hostMode,$state.game_root,$payload,$run,$state.sid,$state.personal_root,$Generation,$Desktop)
if($M06SessionConfig){$workerArguments+=@($sessionTarget)}
$workerArguments+=@('--display-mode','windowed','--resolution','1280x720')
$process=Start-DetachedWorkerSupervisor -Executable (Join-Path $payload 'SporeMP.NativeHost.exe') `
    -WorkingDirectory $payload -Arguments $workerArguments -Credential $workerCredential -RunDirectory $run
$current=[ordered]@{schema_version=1;worker_id=$WorkerId;generation=$Generation;supervisor_pid=$process.Id;run=$run;payload=$payload;supervisor_sha256=$hash;desktop=$Desktop;started_utc=(Get-Date).ToUniversalTime().ToString('o')}
$temporary=Join-Path $stateRoot ($Generation+'.tmp')
$current | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding utf8NoBOM
Move-Item -LiteralPath $temporary -Destination (Join-Path $stateRoot 'current.json') -Force
$current | ConvertTo-Json -Compress
} finally { $launchLock.Dispose() }
