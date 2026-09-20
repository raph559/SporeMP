[CmdletBinding()]
param(
    [ValidateSet('Stage','Refresh','Restore')][string]$Mode='Stage',
    [ValidatePattern('^[a-zA-Z0-9_-]{1,64}$')][string]$StageName=('launcher-'+(Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$ManifestPath,
    [switch]$SkipProbe
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$profileStage='C:\ProgramData\SporeMP\M04\03'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Staging this disposable-account launcher test requires the developer administrator.'}

function Assert-NoReparse([string]$Path){
    $at=[IO.Path]::GetFullPath($Path)
    while($at){
        if((Test-Path -LiteralPath $at) -and ((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw ('Reparse path refused: '+$at)}
        $at=[IO.Path]::GetDirectoryName($at)
    }
}
function Write-NewJson([string]$Path,$Value){
    Assert-NoReparse $Path
    if(Test-Path -LiteralPath $Path){throw 'Evidence/configuration already exists.'}
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
function Protect-NewDirectory([string]$Path,[string]$Sid,[Security.AccessControl.FileSystemRights]$Rights){
    $acl=[Security.AccessControl.DirectorySecurity]::new();$acl.SetAccessRuleProtection($true,$false)
    foreach($systemSid in @('S-1-5-18','S-1-5-32-544')){
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($systemSid),'FullControl','ContainerInherit,ObjectInherit','None','Allow'))
    }
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($Sid),$Rights,'ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $Path -AclObject $acl
}
function Restore-RecordedAcl($Record){
    $path=[IO.Path]::GetFullPath([string]$Record.path)
    if($path -notmatch '^C:\\ProgramData\\SporeMP\\M04\\0[12](?:\\runs(?:\\[a-f0-9]{32}(?:\\[^\\]+)?)?)?$'){
        throw 'ACL restoration path escaped a specifically recorded peer run.'
    }
    Assert-NoReparse $path
    if(-not(Test-Path -LiteralPath $path)){throw 'Recorded ACL target disappeared.'}
    $acl=Get-Acl -LiteralPath $path
    $acl.SetSecurityDescriptorSddlForm([string]$Record.sddl)
    Set-Acl -LiteralPath $path -AclObject $acl
}
function Grant-PeerParentAttributes([string]$WorkerId,$Sid,$Records){
    if($WorkerId -notin @('01','02')){throw 'Only the two exact peer parent directories may be inspected.'}
    foreach($path in @("C:\ProgramData\SporeMP\M04\$WorkerId","C:\ProgramData\SporeMP\M04\$WorkerId\runs")){
        Assert-NoReparse $path
        $acl=Get-Acl -LiteralPath $path
        $Records.Add([ordered]@{path=$path;sddl=$acl.Sddl})
        # Python's unchanged reparse guard must lstat each ancestor. This ACE
        # permits attributes on this directory only, never listing or inheritance.
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($Sid,'ReadAttributes','None','None','Allow'))
        Set-Acl -LiteralPath $path -AclObject $acl
    }
}

if($Mode -eq 'Restore'){
    if(-not $ManifestPath){throw 'Restore requires the staged manifest path.'}
    $manifestAbsolute=[IO.Path]::GetFullPath($ManifestPath)
    if(-not $manifestAbsolute.StartsWith($profileStage+'\launcher-tests\',[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest is outside the developer launcher-test root.'}
    Assert-NoReparse $manifestAbsolute
    $manifest=Get-Content -LiteralPath $manifestAbsolute -Raw | ConvertFrom-Json
    if($manifest.kind -ne 'sporemp-m06-disposable-launcher-stage' -or $manifest.schema_version -ne 1){throw 'Unknown stage manifest.'}
    # Restore parent directories first, then the previously protected secret files.
    foreach($record in $manifest.original_acls){Restore-RecordedAcl $record}
    [ordered]@{restored=$true;manifest=$manifestAbsolute;native_game_started=$false} | ConvertTo-Json -Compress
    return
}

if($Mode -eq 'Refresh'){
    if(-not $ManifestPath){throw 'Refresh requires the existing stage manifest.'}
    $manifestAbsolute=[IO.Path]::GetFullPath($ManifestPath)
    if(-not $manifestAbsolute.StartsWith($profileStage+'\launcher-tests\',[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest is outside the developer launcher-test root.'}
    Assert-NoReparse $manifestAbsolute
    $existing=Get-Content -LiteralPath $manifestAbsolute -Raw | ConvertFrom-Json
    if($existing.kind -ne 'sporemp-m06-disposable-launcher-stage' -or $existing.schema_version -ne 1){throw 'Unknown stage manifest.'}
    $refreshStage=[IO.Path]::GetFullPath([string]$existing.stage)
    if($refreshStage -ne [IO.Path]::GetDirectoryName($manifestAbsolute)){throw 'Stage manifest/root mismatch.'}
    $targetUser=Get-LocalUser -Name 'SporeMP-M04-03'
    if($targetUser.SID.Value -ne $existing.sid){throw 'Disposable account03 identity changed.'}
    $refreshId=[Guid]::NewGuid().ToString('N')
    $refreshAcls=[Collections.Generic.List[object]]::new()
    $refreshCopies=[Collections.Generic.List[object]]::new()
    $refreshPeers=@{};$requiredGamePids=@()
    $refresh=[ordered]@{schema_version=1;kind=$existing.kind;stage=$refreshStage;user=$existing.user;sid=$existing.sid;
        utc=(Get-Date).ToUniversalTime().ToString('o');original_acls=$refreshAcls;copies=$refreshCopies;peer_generations=@{};
        native_game_started=$false;desktop_input=$false;prerequisite_probe='NOT_RUN'}
    $refreshManifest=Join-Path $refreshStage "stage-refresh-$refreshId.json"
    try {
        foreach($id in @('01','02','03')){
            $state=Join-Path $repoRoot "local/worker-accounts/$id"
            $current=Get-Content -LiteralPath (Join-Path $state 'current.json') -Raw | ConvertFrom-Json
            $account=Get-Content -LiteralPath (Join-Path $state 'account.json') -Raw | ConvertFrom-Json
            if($current.worker_id -ne $id -or $current.generation -notmatch '^[a-f0-9]{32}$' -or
               $current.run -ne "C:\ProgramData\SporeMP\M04\$id\runs\$($current.generation)" -or $account.sid -ne (Get-LocalUser -Name "SporeMP-M04-$id").SID.Value){throw 'Current peer registration changed.'}
            Assert-NoReparse $current.run;Assert-NoReparse $current.payload
            if($id -ne '03'){
                Grant-PeerParentAttributes $id $targetUser.SID $refreshAcls
                $process=Get-Process -Id $current.supervisor_pid -ErrorAction SilentlyContinue
                if(-not $process -or $process.Path -ne (Join-Path $current.payload 'SporeMP.NativeHost.exe')){throw 'Refresh needs both current original peer supervisors running.'}
                $status=Get-Content -LiteralPath (Join-Path $current.run 'worker-status.json') -Raw | ConvertFrom-Json
                if($status.generation -ne $current.generation -or $status.game_pid -le 0){throw 'Current peer game identity unavailable.'}
                $requiredGamePids+=@([int]$status.game_pid)
                $refreshAcls.Add([ordered]@{path=$current.run;sddl=(Get-Acl -LiteralPath $current.run).Sddl})
                foreach($secret in Get-ChildItem -LiteralPath $current.run -File | Where-Object {$_.Extension -in @('.cfg','.conf','.invite','.clixml','.pfx','.key')}){
                    Assert-NoReparse $secret.FullName
                    $acl=Get-Acl -LiteralPath $secret.FullName
                    $refreshAcls.Add([ordered]@{path=$secret.FullName;sddl=$acl.Sddl})
                    $acl.SetAccessRuleProtection($true,$true);Set-Acl -LiteralPath $secret.FullName -AclObject $acl
                }
                $acl=Get-Acl -LiteralPath $current.run
                $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($targetUser.SID,'ReadAndExecute','ContainerInherit,ObjectInherit','None','Allow'))
                Set-Acl -LiteralPath $current.run -AclObject $acl
            }
            $refreshPeers[$id]=$current;$refresh.peer_generations[$id]=$current.generation
            $public=Join-Path $refreshStage "local/worker-accounts/$id"
            $temporary=Join-Path $public "current-$refreshId.tmp"
            Write-NewJson $temporary @{schema_version=1;worker_id=$id;generation=$current.generation;supervisor_pid=$current.supervisor_pid;run=$current.run;payload=$current.payload}
            Move-Item -LiteralPath $temporary -Destination (Join-Path $public 'current.json') -Force
        }
        $payload=[string]$refreshPeers['01'].payload
        if((Get-FileHash -LiteralPath (Join-Path $payload 'mLibs/SporeMP.Bridge.dll')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $refreshPeers['02'].payload 'mLibs/SporeMP.Bridge.dll')).Hash){throw 'Current peer bridge builds differ.'}
        foreach($pair in @(@('SporeMP.NativeHost.exe','build/win32/Release/SporeMP.NativeHost.exe'),@('mLibs/SporeMP.Bridge.dll','build/win32/Release/SporeMP.Bridge.dll'),@('mLibs/SporeModAPI.dll','build/sdk/Release/SporeModAPI.dll'),@('ModAPI.DLLInjector.dll','build/injector/Release/ModAPI.DLLInjector.dll'))){
            $source=Join-Path $payload $pair[0];$target=Join-Path $refreshStage $pair[1]
            $hash=(Get-FileHash -LiteralPath $source).Hash
            Copy-Item -LiteralPath $source -Destination $target -Force
            if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Refreshed native payload copy mismatch.'}
            $refreshCopies.Add(@{source=$source;staged=$target;sha256=$hash.ToLowerInvariant()})
        }
        $probePath=Join-Path $refreshStage "refresh-probe-$refreshId.py"
        $probeInput=Join-Path $refreshStage "refresh-probe-$refreshId.json"
        $probeOutput=Join-Path $refreshStage "local/launcher/prerequisite-$refreshId.json"
        Write-NewJson $probeInput @{required_game_pids=$requiredGamePids;output=$probeOutput}
        $code=@'
import json,pathlib,sys
root=pathlib.Path(__file__).resolve().parent
spec=json.loads(pathlib.Path(__file__).with_suffix('.json').read_text())
sys.path.insert(0,str(root/'tools/launcher'))
import launcher_service as service
import worker_manager as workers
result={'schema_version':1,'evidence_class':'DISPOSABLE_ACCOUNT_LAUNCHER_PREREQUISITE','native_game_started':False,'desktop_input':False}
try:
    folders=service.diag.known_folders(); result['known_folders']=folders
    if folders['errors'] or pathlib.Path(folders['shell_folders']['appdata'])!=pathlib.Path('C:/Users/SporeMP-M04-03/AppData/Roaming'): raise RuntimeError('Wrong actual disposable shell folders')
    games=workers.checked_games();result['registered_original_games']=games
    if not set(spec['required_game_pids']).issubset({game['pid'] for game in games}): raise RuntimeError('Required current peer game PIDs are not all present')
    result['fixture_sha256']=service.diag.fingerprint(pathlib.Path(folders['shell_folders']['appdata'])/'Spore/Games/Game0/Satiria.spo')['sha256']
    for worker in ('01','02'):
        run=pathlib.Path(workers.current(worker)['run'])
        for suffix in ('*.cfg','*.conf','*.invite','*.clixml','*.pfx','*.key'):
            for path in run.glob(suffix):
                try:
                    with path.open('rb') as stream: stream.read(1)
                except PermissionError: continue
                raise RuntimeError('Peer secret is unexpectedly readable')
    prepared,code=service.prepare(pathlib.Path('C:/Games/SPORE'))
    result['preparation_state']=prepared['state'];result['preparation_exit']=code
    if code!=22 or prepared['state']!='development_build' or not prepared['native']['join_available']: raise RuntimeError('Unchanged concurrent preparation did not become ready')
    result['prerequisites_passed']=True
except Exception as error: result['prerequisites_passed']=False;result['error']=str(error)
service.diag.write_json(pathlib.Path(spec['output']),result)
print(json.dumps({'prerequisites_passed':result['prerequisites_passed'],'report':spec['output'],'native_game_started':False}))
raise SystemExit(0 if result['prerequisites_passed'] else 1)
'@
        [IO.File]::WriteAllText($probePath,$code,[Text.UTF8Encoding]::new($false))
        if(-not $SkipProbe){
            $savedCredential=Import-Clixml -LiteralPath (Join-Path $repoRoot 'local/worker-accounts/03/credential.clixml')
            $credential=[pscredential]::new("$env:COMPUTERNAME\SporeMP-M04-03",$savedCredential.Password)
            $argument='"'+($probePath -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'
            $process=Start-Process -FilePath (Join-Path $refreshStage 'runtime/python/python.exe') -ArgumentList $argument -WorkingDirectory $refreshStage `
                -Credential $credential -LoadUserProfile -UseNewEnvironment -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput (Join-Path $refreshStage "local/launcher/refresh-$refreshId.stdout.log") -RedirectStandardError (Join-Path $refreshStage "local/launcher/refresh-$refreshId.stderr.log")
            if(-not $process.WaitForExit(90000)){$process.Kill($true);throw 'Read-only concurrent prerequisite exceeded 90 seconds.'}
            $refresh.prerequisite_probe=$process.ExitCode;$refresh.prerequisite_report=$probeOutput
        }
    } catch {$refresh.error=$_.Exception.Message}
    finally {Write-NewJson $refreshManifest $refresh}
    [ordered]@{stage=$refreshStage;manifest=$refreshManifest;prerequisite_probe=$refresh.prerequisite_probe;prerequisite_report=$refresh.prerequisite_report;error=$refresh.error;native_game_started=$false} | ConvertTo-Json -Compress
    if($refresh.error -or ($refresh.prerequisite_probe -ne 'NOT_RUN' -and $refresh.prerequisite_probe -ne 0)){exit 1}
    return
}

$stageRoot=[IO.Path]::GetFullPath((Join-Path $profileStage ('launcher-tests/'+$StageName)))
if(-not $stageRoot.StartsWith($profileStage+'\launcher-tests\',[StringComparison]::OrdinalIgnoreCase)){throw 'Stage escaped its named developer root.'}
Assert-NoReparse $stageRoot
if(Test-Path -LiteralPath $stageRoot){throw 'Use a fresh launcher stage name; existing work is preserved.'}
$targetUser=Get-LocalUser -Name 'SporeMP-M04-03'
$targetSid=$targetUser.SID.Value
$account03=Get-Content -LiteralPath (Join-Path $repoRoot 'local/worker-accounts/03/account.json') -Raw | ConvertFrom-Json
if(-not $account03.prepared -or $account03.sid -ne $targetSid -or $account03.profile -ne 'C:\Users\SporeMP-M04-03' -or $account03.stage_root -ne $profileStage){throw 'Disposable account03 identity changed.'}

$currents=@{};$accounts=@{}
foreach($id in @('01','02','03')){
    $stateRoot=Join-Path $repoRoot "local/worker-accounts/$id"
    Assert-NoReparse $stateRoot
    $account=Get-Content -LiteralPath (Join-Path $stateRoot 'account.json') -Raw | ConvertFrom-Json
    $actualUser=Get-LocalUser -Name "SporeMP-M04-$id"
    if(-not $account.prepared -or $account.sid -ne $actualUser.SID.Value -or $account.user -ne $actualUser.Name){throw 'Peer account identity changed.'}
    $accounts[$id]=$account
    $currentPath=Join-Path $stateRoot 'current.json'
    if(Test-Path -LiteralPath $currentPath){
        $current=Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
        if($current.worker_id -ne $id -or $current.generation -notmatch '^[a-f0-9]{32}$' -or
           $current.run -ne "C:\ProgramData\SporeMP\M04\$id\runs\$($current.generation)"){throw 'Peer current-generation path changed.'}
        Assert-NoReparse $current.run;Assert-NoReparse $current.payload
        $currents[$id]=$current
    }
    if($id -ne '03'){
        if(-not $currents.ContainsKey($id)){throw 'Start the current authority01/client02 before staging their public registration metadata.'}
        $peerProcess=Get-Process -Id $currents[$id].supervisor_pid -ErrorAction SilentlyContinue
        if(-not $peerProcess -or $peerProcess.Path -ne (Join-Path $currents[$id].payload 'SporeMP.NativeHost.exe')){throw 'Peer supervisor is not currently running from its registered payload.'}
    }
}

# Use the current authority's already-guarded native payload. Profile03 may still
# point at an earlier, closed acceptance attempt; never stage that stale bridge.
$nativePayload=[string]$currents['01'].payload
$authorityBridge=Join-Path $nativePayload 'mLibs/SporeMP.Bridge.dll'
if((Get-FileHash -LiteralPath $authorityBridge).Hash -ne (Get-FileHash -LiteralPath (Join-Path $currents['02'].payload 'mLibs/SporeMP.Bridge.dll')).Hash){throw 'Current peer native bridge builds disagree.'}
$pythonBase=(& python -c 'import sys; print(sys.base_prefix)')
if($LASTEXITCODE -ne 0 -or -not $pythonBase){throw 'Installed standalone Python runtime could not be resolved.'}
$pythonBase=[IO.Path]::GetFullPath([string]$pythonBase)
$pythonDirectory=Get-Item -LiteralPath $pythonBase -Force
if($pythonDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint){
    # uv's version alias is a junction. Resolve that discovered installed runtime
    # once, then require its final physical directory/tree to contain no reparse.
    $pythonTarget=$pythonDirectory.ResolveLinkTarget($true)
    if(-not $pythonTarget -or -not $pythonTarget.PSIsContainer){
        if(-not ($pythonTarget -is [IO.DirectoryInfo])){throw 'Python alias did not resolve to a physical runtime directory.'}
    }
    $pythonBase=$pythonTarget.FullName
}
Assert-NoReparse $pythonBase
if(-not(Test-Path -LiteralPath (Join-Path $pythonBase 'python.exe'))){throw 'Python base runtime is incomplete.'}
if(Get-ChildItem -LiteralPath $pythonBase -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Python runtime contains reparse paths; stage was not copied.'}

New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
Protect-NewDirectory $stageRoot $targetSid 'ReadAndExecute'
foreach($directory in @('tools/launcher','tools/diagnostics','config','build/launcher/Release','build/win32/Release','build/sdk/Release','build/injector/Release','runtime/python','local/launcher','local/worker-accounts')){
    New-Item -ItemType Directory -Path (Join-Path $stageRoot $directory) -Force | Out-Null
}
Protect-NewDirectory (Join-Path $stageRoot 'local/launcher') $targetSid 'Modify'
$copies=[Collections.Generic.List[object]]::new()
function Copy-Verified([string]$Source,[string]$Relative){
    Assert-NoReparse $Source
    $destination=[IO.Path]::GetFullPath((Join-Path $stageRoot $Relative))
    if(-not $destination.StartsWith($stageRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Copied file escaped staged checkout.'}
    Assert-NoReparse $destination
    $hash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $Source -Destination $destination
    if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash -or (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $hash){throw 'Source changed or staging copy differs.'}
    $copies.Add([ordered]@{source=$Source;staged=$destination;sha256=$hash.ToLowerInvariant()})
}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tools/launcher') -Filter '*.py' -File){Copy-Verified $file.FullName ('tools/launcher/'+$file.Name)}
Copy-Verified (Join-Path $repoRoot 'tools/diagnostics/sporemp_diag.py') 'tools/diagnostics/sporemp_diag.py'
Copy-Verified (Join-Path $repoRoot 'config/compatibility.candidate.json') 'config/compatibility.candidate.json'
foreach($filename in @('SporeMP.exe','SporeMP.dll','SporeMP.deps.json','SporeMP.runtimeconfig.json')){
    Copy-Verified (Join-Path $repoRoot "build/launcher/Release/$filename") ("build/launcher/Release/$filename")
}
foreach($pair in @(
    @('SporeMP.NativeHost.exe','build/win32/Release/SporeMP.NativeHost.exe'),
    @('mLibs/SporeMP.Bridge.dll','build/win32/Release/SporeMP.Bridge.dll'),
    @('mLibs/SporeModAPI.dll','build/sdk/Release/SporeModAPI.dll'),
    @('ModAPI.DLLInjector.dll','build/injector/Release/ModAPI.DLLInjector.dll')
)){Copy-Verified (Join-Path $nativePayload $pair[0]) $pair[1]}
foreach($filename in @('SporeMP.Coordinator.exe','SporeMP.WorkerControl.exe')){
    Copy-Verified (Join-Path $repoRoot "build/win32/Release/$filename") ("build/win32/Release/$filename")
}
# Copy an already-installed stdlib runtime, never install/fetch Python or grant
# the disposable account access to the developer's personal profile.
foreach($file in Get-ChildItem -LiteralPath $pythonBase -Recurse -File){
    $relative=[IO.Path]::GetRelativePath($pythonBase,$file.FullName)
    $destination=Join-Path $stageRoot ('runtime/python/'+$relative)
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Verified $file.FullName ('runtime/python/'+$relative)
}
$stagedPython=Join-Path $stageRoot 'runtime/python/python.exe'
Write-NewJson (Join-Path $stageRoot 'build/launcher/Release/launcher.runtime.json') @{repo_root=$stageRoot;python_executable=$stagedPython}
Write-NewJson (Join-Path $stageRoot 'local/launcher/settings.json') @{schema_version=1;game_root='C:\Games\SPORE';display=@{mode='windowed';resolution='1280x720'}}

$originalAcls=[Collections.Generic.List[object]]::new()
$manifest=[ordered]@{schema_version=1;kind='sporemp-m06-disposable-launcher-stage';stage=$stageRoot;utc=(Get-Date).ToUniversalTime().ToString('o');
    user='SporeMP-M04-03';sid=$targetSid;native_game_started=$false;desktop_input=$false;native_payload_source=$nativePayload;
    original_acls=$originalAcls;copies=$copies;peer_generations=@{};prerequisite_probe='NOT_RUN';launcher=(Join-Path $stageRoot 'build/launcher/Release/SporeMP.exe')}
$manifestTarget=Join-Path $stageRoot 'stage-manifest.json'
try {
    foreach($id in @('01','02','03')){
        $publicRoot=Join-Path $stageRoot "local/worker-accounts/$id"
        New-Item -ItemType Directory -Path $publicRoot -Force | Out-Null
        # Explicit public registration subset, never credential.clixml or saves.
        Write-NewJson (Join-Path $publicRoot 'account.json') @{schema_version=1;prepared=$true;worker_id=$id;user=$accounts[$id].user;sid=$accounts[$id].sid;game_root=$accounts[$id].game_root}
        if($currents.ContainsKey($id)){
            $current=$currents[$id]
            Write-NewJson (Join-Path $publicRoot 'current.json') @{schema_version=1;worker_id=$id;generation=$current.generation;supervisor_pid=$current.supervisor_pid;run=$current.run;payload=$current.payload}
            $manifest.peer_generations[$id]=$current.generation
        }
        if($id -eq '03'){continue}
        Grant-PeerParentAttributes $id $targetUser.SID $originalAcls
        $run=[string]$currents[$id].run
        $originalAcls.Add([ordered]@{path=$run;sddl=(Get-Acl -LiteralPath $run).Sddl})
        foreach($secret in Get-ChildItem -LiteralPath $run -File | Where-Object {$_.Extension -in @('.cfg','.conf','.invite','.clixml','.pfx','.key')}){
            Assert-NoReparse $secret.FullName
            $acl=Get-Acl -LiteralPath $secret.FullName
            $originalAcls.Add([ordered]@{path=$secret.FullName;sddl=$acl.Sddl})
            $acl.SetAccessRuleProtection($true,$true)
            Set-Acl -LiteralPath $secret.FullName -AclObject $acl
        }
        # Status is atomically replaced. Read-only inheritance on this exact
        # current run keeps replacement status readable; protected configs above
        # retain Admin/SYSTEM/own-peer permissions and never inherit this grant.
        $acl=Get-Acl -LiteralPath $run
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($targetUser.SID,'ReadAndExecute','ContainerInherit,ObjectInherit','None','Allow'))
        Set-Acl -LiteralPath $run -AclObject $acl
    }
    foreach($id in @('01','02')){
        $latest=Get-Content -LiteralPath (Join-Path $repoRoot "local/worker-accounts/$id/current.json") -Raw | ConvertFrom-Json
        if($latest.generation -ne $currents[$id].generation -or $latest.supervisor_pid -ne $currents[$id].supervisor_pid){throw 'Peer generation changed during staging; create a new stage.'}
    }
    $probePath=Join-Path $stageRoot 'prerequisite-probe.py'
    $probeCode=@'
import ctypes,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(root/'tools/launcher'))
import launcher_service as service
import worker_manager as workers
result={'schema_version':1,'evidence_class':'DISPOSABLE_ACCOUNT_LAUNCHER_PREREQUISITE','native_game_started':False,'desktop_input':False}
try:
    folders=service.diag.known_folders()
    result['known_folders']=folders
    expected=pathlib.Path('C:/Users/SporeMP-M04-03/AppData/Roaming')
    if folders['errors'] or pathlib.Path(folders['shell_folders']['appdata'])!=expected: raise RuntimeError('Actual shell folders are not the qualified disposable account03')
    fixture=expected/'Spore/Games/Game0/Satiria.spo'
    result['fixture_sha256']=service.diag.fingerprint(fixture)['sha256']
    result['registered_original_games']=workers.checked_games()
    result['native_components']=service.native_availability()
    prepared,code=service.prepare(pathlib.Path('C:/Games/SPORE'))
    result['preparation_state']=prepared['state'];result['preparation_exit']=code
    if code!=22 or prepared['state']!='development_build' or not prepared['native']['join_available']: raise RuntimeError('Unchanged launcher preparation did not become ready')
    for worker_id in ('01','02'):
        run=pathlib.Path(workers.current(worker_id)['run'])
        # Verify metadata is readable but each existing token config remains denied.
        for suffix in ('*.cfg','*.conf','*.invite','*.clixml','*.pfx','*.key'):
            for path in run.glob(suffix):
                try:
                    with path.open('rb') as stream: stream.read(1)
                except PermissionError: continue
                raise RuntimeError('Peer secret is unexpectedly readable')
    result['prerequisites_passed']=True
except Exception as error:
    result['prerequisites_passed']=False;result['error']=str(error)
service.diag.write_json(root/'local/launcher/prerequisite.json',result)
print(json.dumps({'prerequisites_passed':result['prerequisites_passed'],'report':str(root/'local/launcher/prerequisite.json'),'native_game_started':False}))
raise SystemExit(0 if result['prerequisites_passed'] else 1)
'@
    [IO.File]::WriteAllText($probePath,$probeCode,[Text.UTF8Encoding]::new($false))
    if(-not $SkipProbe){
        $savedCredential=Import-Clixml -LiteralPath (Join-Path $repoRoot 'local/worker-accounts/03/credential.clixml')
        $credential=[pscredential]::new("$env:COMPUTERNAME\SporeMP-M04-03",$savedCredential.Password)
        $quotedProbe='"'+($probePath -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'
        $probe=Start-Process -FilePath $stagedPython -ArgumentList $quotedProbe -WorkingDirectory $stageRoot `
            -Credential $credential -LoadUserProfile -UseNewEnvironment -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $stageRoot 'local/launcher/probe.stdout.log') -RedirectStandardError (Join-Path $stageRoot 'local/launcher/probe.stderr.log')
        if(-not $probe.WaitForExit(90000)){$probe.Kill($true);throw 'The no-game prerequisite probe exceeded 90 seconds.'}
        $manifest.prerequisite_probe=$probe.ExitCode
        $probeReport=Join-Path $stageRoot 'local/launcher/prerequisite.json'
        if(Test-Path -LiteralPath $probeReport){$manifest.prerequisite_report=$probeReport}
        if($probe.ExitCode -ne 0){$manifest.prerequisite_error='Read the recorded prerequisite report; no launcher or game was started.'}
    }
} catch {
    $manifest.error=$_.Exception.Message
} finally {
    Write-NewJson $manifestTarget $manifest
}
[ordered]@{stage=$stageRoot;manifest=$manifestTarget;launcher=$manifest.launcher;user=$manifest.user;
    prerequisite_probe=$manifest.prerequisite_probe;error=$manifest.error;native_game_started=$false;desktop_input=$false} | ConvertTo-Json -Compress
if($manifest.error -or ($manifest.prerequisite_probe -ne 'NOT_RUN' -and $manifest.prerequisite_probe -ne 0)){exit 1}
