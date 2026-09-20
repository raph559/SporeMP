[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]{1,35}$')][string]$RunKey,
    [switch]$ReuseDatabase,
    [ValidatePattern('^[0-9A-Fa-f]{6,8}(,[0-9A-Fa-f]{6,8})*$')][string]$Addresses
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolRoot = Join-Path $repoRoot 'external/re-tools'
$analysisRoot = Join-Path $repoRoot 'local/m03-static'
$projectRoot = Join-Path $analysisRoot 'projects'
$output = Join-Path $analysisRoot $RunKey
$reportPath = Join-Path $analysisRoot ($RunKey + '.json')
$game = 'C:\Games\SPORE\SporebinEP1\SporeApp.exe'
$expected = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
if ((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Unknown executable.' }
if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $reportPath)) { throw 'Static evidence already exists.' }
$headless = Join-Path $toolRoot 'ghidra_12.1.3_PUBLIC/support/analyzeHeadless.bat'
$jdk = Join-Path $toolRoot 'jdk-21.0.12.1+1'
if (-not (Test-Path -LiteralPath $headless) -or -not (Test-Path -LiteralPath (Join-Path $jdk 'bin/java.exe'))) {
    throw 'Run the pinned tools/build/fetch-re-tools.ps1 first.'
}
New-Item -ItemType Directory -Path $projectRoot -Force | Out-Null
$project = Join-Path $projectRoot 'SporePinned.gpr'
if ($ReuseDatabase -and -not (Test-Path -LiteralPath $project)) { throw 'No analyzed database exists.' }
if (-not $ReuseDatabase -and (Test-Path -LiteralPath $project)) { throw 'Database already exists; use ReuseDatabase.' }
$arguments = @($projectRoot,'SporePinned')
if ($ReuseDatabase) { $arguments += @('-process','SporeApp.exe','-noanalysis') }
else { $arguments += @('-import',$game,'-analysisTimeoutPerFile','300') }
$arguments += @('-scriptPath',(Join-Path $repoRoot 'tools/native/ghidra'))
$labels = Join-Path $analysisRoot 'sdk-symbols.tsv'
if (Test-Path -LiteralPath $labels) { $arguments += @('-preScript','SporeSdkLabels.java',$labels) }
$arguments += @('-postScript','SporeM03Audit.java',$output)
if ($Addresses) { $arguments += $Addresses.Split(',') }
$arguments += @('-max-cpu','8','-log',(Join-Path $analysisRoot ($RunKey+'-app.log')),
    '-scriptlog',(Join-Path $analysisRoot ($RunKey+'-script.log')))
$savedPath=$env:PATH
$savedJava=$env:JAVA_HOME
$savedHeap=$env:GHIDRA_HEADLESS_MAXMEM
$started=(Get-Date).ToUniversalTime().ToString('o')
$exitCode=2
try {
    $env:JAVA_HOME=$jdk
    $env:PATH=(Join-Path $jdk 'bin') + ';' + $savedPath
    $env:GHIDRA_HEADLESS_MAXMEM='6G'
    & $headless @arguments
    $exitCode=$LASTEXITCODE
    if ($exitCode -eq 0) {
        $index = Join-Path $output 'index.txt'
        $appLog = Join-Path $analysisRoot ($RunKey+'-app.log')
        if (-not (Test-Path -LiteralPath $index) -or (Select-String -LiteralPath $appLog -Pattern 'REPORT SCRIPT ERROR' -Quiet)) {
            Write-Output 'Static export failed despite the headless process exit code; see the application log.'
            $exitCode=3
        } elseif (Select-String -LiteralPath $index -Pattern 'MISSING FUNCTION|completed=false' -Quiet) {
            Write-Output 'Some requested functions could not be decompiled; the static export is partial.'
            $exitCode=4
        }
    }
} finally {
    $env:PATH=$savedPath
    $env:JAVA_HOME=$savedJava
    $env:GHIDRA_HEADLESS_MAXMEM=$savedHeap
    [ordered]@{evidence_class='STATIC_DECOMPILATION_NOT_NATIVE_EXECUTION';command=@($headless)+$arguments;
        expected_exit=0;observed_exit=$exitCode;started_utc=$started;ended_utc=(Get-Date).ToUniversalTime().ToString('o');
        executable_sha256=$expected;ghidra_version='12.1.3';jdk_version='21.0.12.1+1';
        script_sha256=(Get-FileHash -LiteralPath (Join-Path $repoRoot 'tools/native/ghidra/SporeM03Audit.java')).Hash.ToLowerInvariant();
        output=$output;native_execution='NOT RUN'} | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
}
exit $exitCode
