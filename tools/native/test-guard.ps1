[CmdletBinding()]
param([string]$RunName = ('guard-tests-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ($RunName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid run name.' }
if (Get-Process -Name SporeApp -ErrorAction SilentlyContinue) { throw 'Close the game normally before negative guard tests.' }
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'invoke-isolated.ps1') -Action NativeProbe -RunName ($RunName+'-baseline')
if ($LASTEXITCODE -ne 0) { throw 'Matching baseline did not pass.' }
$state = Get-Content (Join-Path $repoRoot 'local\native-account\account.json') -Raw | ConvertFrom-Json
$credential = Import-Clixml (Join-Path $repoRoot 'local\native-account\credential.clixml')
$hash = (Get-FileHash (Join-Path $repoRoot 'build\win32\Release\SporeMP.NativeHost.exe')).Hash.ToLowerInvariant()
$payload = Join-Path $state.stage_root "payload\$hash"
$root = Join-Path $state.stage_root "negative\$RunName"
if (Test-Path -LiteralPath $root) { throw 'Negative test destination exists.' }
New-Item -ItemType Directory -Path $root | Out-Null
function New-ResultDirectory([string]$Name) {
    $path = Join-Path $root $Name
    New-Item -ItemType Directory -Path $path | Out-Null
    $acl = Get-Acl -LiteralPath $path
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($state.sid),'Modify','ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $path -AclObject $acl
    return $path
}
function Change-CopyByte([string]$Path) {
    $stream = [IO.File]::Open($Path,'Open','ReadWrite','None')
    try { $stream.Position=$stream.Length-1; $byte=$stream.ReadByte(); $stream.Position=$stream.Length-1; $stream.WriteByte($byte -bxor 1) } finally { $stream.Dispose() }
}
$cases = @()
$badGame = Join-Path $root 'executable-copy'
New-Item -ItemType Directory -Path (Join-Path $badGame 'SporebinEP1') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $state.game_root 'SporebinEP1\SporeApp.exe') -Destination (Join-Path $badGame 'SporebinEP1\SporeApp.exe')
Change-CopyByte (Join-Path $badGame 'SporebinEP1\SporeApp.exe')
$cases += @{name='same-size-executable-mutation';game=$badGame;payload=$payload;reason='Fingerprint mismatch';account=$true}
$badContent = Join-Path $root 'content-copy'
foreach ($directory in @('Data','DataEP1','bp1content','SporebinEP1')) { New-Item -ItemType Directory -Path (Join-Path $badContent $directory) -Force | Out-Null }
Copy-Item -LiteralPath (Join-Path $state.game_root 'SporebinEP1\SporeApp.exe') -Destination (Join-Path $badContent 'SporebinEP1\SporeApp.exe')
'synthetic incompatible content' | Set-Content -LiteralPath (Join-Path $badContent 'Data\unexpected.fixture')
$cases += @{name='incompatible-content-set';game=$badContent;payload=$payload;reason='Unknown or missing content entry';account=$true}
foreach ($relative in @('ModAPI.DLLInjector.dll','mLibs\SporeModAPI.dll','mLibs\SporeMP.Bridge.dll')) {
    $name = [IO.Path]::GetFileNameWithoutExtension($relative).Replace('.','-')
    $badPayload = Join-Path $root ("payload-"+$name)
    New-Item -ItemType Directory -Path (Join-Path $badPayload 'mLibs') -Force | Out-Null
    foreach ($file in @('ModAPI.DLLInjector.dll','mLibs\SporeModAPI.dll','mLibs\SporeMP.Bridge.dll')) {
        Copy-Item -LiteralPath (Join-Path $payload $file) -Destination (Join-Path $badPayload $file)
    }
    Change-CopyByte (Join-Path $badPayload $relative)
    $cases += @{name=("changed-"+$name);game=$state.game_root;payload=$badPayload;reason='Fingerprint mismatch';account=$true}
}
$cases += @{name='personal-account-denied';game=$state.game_root;payload=$payload;reason='Dedicated standard account required';account=$false}
$results = foreach ($case in $cases) {
    $run = New-ResultDirectory $case.name
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $payload 'SporeMP.NativeHost.exe'))
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WorkingDirectory=$payload
    if ($case.account) { $info.LoadUserProfile=$true; $info.UserName=$state.user; $info.Domain=$env:COMPUTERNAME; $info.Password=$credential.Password }
    foreach ($arg in @('--launch',$case.game,$case.payload,$run,$state.sid,$state.personal_root)) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Guard probe timed out.' }
    $events = @(Get-Content -LiteralPath (Join-Path $run 'native-host.jsonl') | ConvertFrom-Json)
    $rejection = @($events | Where-Object event -eq 'rejected')
    $created = @($events | Where-Object event -eq 'created_suspended').Count
    $pass = $process.ExitCode -eq 20 -and $created -eq 0 -and $rejection.Count -eq 1 -and $rejection[0].reason.Contains($case.reason)
    [ordered]@{name=$case.name;expected_exit=20;actual_exit=$process.ExitCode;created_game_processes=$created;reason=$rejection[0].reason;passed=$pass;evidence=$run}
}
$result = [ordered]@{schema_version=1;kind='real-guard-negative-fixtures';utc=(Get-Date).ToUniversalTime().ToString('o');host_sha256=$hash;cases=$results;passed=(@($results | Where-Object passed -eq $false).Count -eq 0);original_game_launches=0}
$result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $root 'results.json')
$result | ConvertTo-Json -Depth 7
if (-not $result.passed) { exit 1 }
