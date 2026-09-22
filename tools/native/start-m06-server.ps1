[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]+$')][string]$RunName,
      [ValidateRange(1024,65535)][int]$Port=27060)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root=Join-Path $repo "local/m06-network/$RunName"
if(Test-Path -LiteralPath $root){throw 'Fresh server run directory required.'}
New-Item -ItemType Directory -Path $root | Out-Null
$bridge=Join-Path $repo 'build/win32/Release/SporeMP.Bridge.dll'
$coordinator=Join-Path $repo 'build/win32/Release/SporeMP.Coordinator.exe'
$candidate=Join-Path $repo 'config/compatibility.candidate.json'
$fixture='C:\Users\SporeMP-M04-01\AppData\Roaming\Spore\Games\Game0\Satiria.spo'
$config=Join-Path $root 'server.conf'
$out=Join-Path $root 'private'
$worldOutput = & $coordinator --world-identity --root (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $fixture)))
if($LASTEXITCODE -ne 0){throw ('Canonical world identity check failed: '+$worldOutput)}
$world = $worldOutput | ConvertFrom-Json
$lines=@('schema=2','host=127.0.0.1',"port=$Port",
    ('build_sha256='+((Get-FileHash -LiteralPath $bridge).Hash.ToLowerInvariant())),
    ('executable_sha256='+((Get-Content -LiteralPath $candidate -Raw | ConvertFrom-Json).executable.sha256)),
    ('content_sha256='+((Get-FileHash -LiteralPath $candidate).Hash.ToLowerInvariant())),
    ('fixture_sha256='+((Get-FileHash -LiteralPath $fixture).Hash.ToLowerInvariant())))
foreach($field in $world.fields.PSObject.Properties){$lines += ($field.Name+'='+$field.Value)}
[IO.File]::WriteAllLines($config,$lines,[Text.UTF8Encoding]::new($false))
$arguments=@('--serve','--config',$config,'--output',$out)
$quoted=($arguments|ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}) -join ' '
$stdin=Join-Path $root 'stdin.txt'
[IO.File]::WriteAllText($stdin,'')
$process=Start-Process -FilePath $coordinator -ArgumentList $quoted -WindowStyle Hidden -PassThru -WorkingDirectory $repo -RedirectStandardInput $stdin -RedirectStandardOutput (Join-Path $root 'stdout.log') -RedirectStandardError (Join-Path $root 'stderr.log')
[ordered]@{utc=[DateTime]::UtcNow.ToString('o');pid=$process.Id;executable=$coordinator;executable_sha256=(Get-FileHash -LiteralPath $coordinator).Hash.ToLowerInvariant();arguments=$arguments;root=$root;private=$out;port=$Port} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'server.json') -Encoding utf8NoBOM
$deadline=[DateTime]::UtcNow.AddSeconds(12)
while([DateTime]::UtcNow -lt $deadline -and -not(Test-Path -LiteralPath (Join-Path $out 'player-2.conf')) -and -not $process.HasExited){Start-Sleep -Milliseconds 100;$process.Refresh()}
if($process.HasExited -or -not(Test-Path -LiteralPath (Join-Path $out 'player-2.conf'))){Get-Content -LiteralPath (Join-Path $root 'stdout.log');throw 'Coordinator did not become ready.'}
Write-Output "Coordinator PID $($process.Id), loopback port $Port; private invitation files ready."
