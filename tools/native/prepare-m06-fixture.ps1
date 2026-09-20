[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]+$')][string]$Name)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process -Name SporeApp -ErrorAction SilentlyContinue){throw 'Close every original game before preparing the shared fixture.'}
$root=Join-Path $repo "local/m06-network/$Name"
if(Test-Path -LiteralPath $root){throw 'Fresh fixture evidence path required.'}
function Check-Path([string]$Path) {
    $absolute=[IO.Path]::GetFullPath($Path)
    $at=$absolute
    while($at){
        if((Test-Path -LiteralPath $at) -and ((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Reparse fixture path refused.'}
        $at=[IO.Path]::GetDirectoryName($at)
    }
    return $absolute
}
function Tree-Hashes([string]$Path) {
    $prefix=(Check-Path $Path)+'\'
    @(Get-ChildItem -LiteralPath $Path -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($prefix.Length).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
    })
}
New-Item -ItemType Directory -Path $root | Out-Null
$sourceState=Get-Content -LiteralPath (Join-Path $repo 'local/worker-accounts/02/account.json') -Raw | ConvertFrom-Json
if($sourceState.profile -ne 'C:\Users\SporeMP-M04-02' -or -not $sourceState.prepared){throw 'Source worker profile mismatch.'}
$relative=@('AppData/Roaming/Spore','Documents/My Spore Creations')
$rows=@()
for($i=0;$i -lt $relative.Count;$i++) {
    $source=Check-Path (Join-Path $sourceState.profile $relative[$i])
    $snapshot=Join-Path $root "source-$i"
    Copy-Item -LiteralPath $source -Destination $snapshot -Recurse
    $hashes=Tree-Hashes $snapshot
    if(($hashes|ConvertTo-Json -Compress) -ne ((Tree-Hashes $source)|ConvertTo-Json -Compress)){throw 'Closed snapshot copy mismatch.'}
    $rows+=@([ordered]@{source=$source;snapshot=$snapshot;files=$hashes})
}
foreach($workerId in @('01','03')) {
    $state=Get-Content -LiteralPath (Join-Path $repo "local/worker-accounts/$workerId/account.json") -Raw | ConvertFrom-Json
    $expected="C:\Users\SporeMP-M04-$workerId"
    if($state.profile -ne $expected -or -not $state.prepared){throw 'Destination worker profile mismatch.'}
    for($i=0;$i -lt $relative.Count;$i++) {
        $target=Check-Path (Join-Path $expected $relative[$i])
        $retained=Check-Path ($target+'.pre-m06-'+$Name)
        if(-not $target.StartsWith($expected+'\',[StringComparison]::OrdinalIgnoreCase) -or
           -not $retained.StartsWith($expected+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture destination escaped explicit worker profile.'}
        if(Test-Path -LiteralPath $retained){throw 'Retained fixture path already exists.'}
        $oldAcl=Get-Acl -LiteralPath $target
        # Resolved absolute source/destination verified above; same-shell native
        # move preserves the complete previous disposable fixture for review.
        Move-Item -LiteralPath $target -Destination $retained
        Copy-Item -LiteralPath $rows[$i].snapshot -Destination $target -Recurse
        Set-Acl -LiteralPath $target -AclObject $oldAcl
        if(((Tree-Hashes $target)|ConvertTo-Json -Compress) -ne ($rows[$i].files|ConvertTo-Json -Compress)){throw 'Prepared profile differs from closed fixture.'}
    }
}
[ordered]@{utc=(Get-Date).ToUniversalTime().ToString('o');source_worker='02';prepared_workers=@('01','02','03');sources=$rows;personal_files_modified=$false;native_execution='NOT_RUN'} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'fixture.json') -Encoding utf8NoBOM
Write-Output "Prepared three matching closed disposable fixtures; prior 01/03 contents retained."
