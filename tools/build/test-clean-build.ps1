[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$destinationPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Destination))
$allowedRoot = (Join-Path $repoRoot 'local') + [IO.Path]::DirectorySeparatorChar
if (-not $destinationPath.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Clean-build exports must stay inside this repository local directory.'
}
if (Test-Path -LiteralPath $destinationPath) { throw 'Destination exists; choose a fresh path.' }
New-Item -ItemType Directory -Path (Join-Path $repoRoot 'local') -Force | Out-Null
$runId = [Guid]::NewGuid().ToString('N')
$indexPath = Join-Path $repoRoot "local\qualification-$runId.index"
$archivePath = Join-Path $repoRoot "local\qualification-$runId.zip"
$priorIndex = $env:GIT_INDEX_FILE
try {
    # A temporary index records an immutable source tree without staging user changes.
    $env:GIT_INDEX_FILE = $indexPath
    & git -C $repoRoot read-tree --empty
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize source snapshot index.' }
    & git -C $repoRoot add --all -- . ':!evidence'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot snapshot project sources.' }
    $sourceTree = & git -C $repoRoot write-tree
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record source tree.' }
    & git -C $repoRoot archive --format=zip "--output=$archivePath" $sourceTree
    if ($LASTEXITCODE -ne 0) { throw 'Cannot export source tree.' }
} finally {
    if ($null -eq $priorIndex) { Remove-Item Env:\GIT_INDEX_FILE -ErrorAction SilentlyContinue }
    else { $env:GIT_INDEX_FILE = $priorIndex }
}
Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath
Write-Host "Clean source tree: $sourceTree"
Write-Host "Clean export: $destinationPath (no external dependencies or build cache)"
& pwsh -NoProfile -File (Join-Path $destinationPath 'tools\build\build.ps1') -Configuration Release -FetchDependencies 2>&1 | Tee-Object -FilePath (Join-Path $destinationPath 'clean-build.log')
$buildExit = $LASTEXITCODE
if ($buildExit -ne 0) { throw "Clean build failed: $buildExit" }
& ctest --test-dir (Join-Path $destinationPath 'build\win32') -C Release --output-on-failure -V 2>&1 | Tee-Object -FilePath (Join-Path $destinationPath 'clean-tests.log')
$testExit = $LASTEXITCODE
if ($testExit -ne 0) { throw "Clean tests failed: $testExit" }
$report = [ordered]@{
    schema_version = 1
    observed_utc = (Get-Date).ToUniversalTime().ToString('o')
    evidence_kind = 'clean-source-export-build-and-host-tests'
    source_tree = $sourceTree
    source_archive_sha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    destination = $destinationPath
    build_exit = $buildExit
    tests_exit = $testExit
    bridge_sha256 = (Get-FileHash -LiteralPath (Join-Path $destinationPath 'build\win32\Release\SporeMP.Bridge.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    sdk_sha256 = (Get-FileHash -LiteralPath (Join-Path $destinationPath 'build\sdk\Release\SporeModAPI.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    launcher_exe_sha256 = (Get-FileHash -LiteralPath (Join-Path $destinationPath 'build\launcher\Release\SporeMP.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    launcher_managed_sha256 = (Get-FileHash -LiteralPath (Join-Path $destinationPath 'build\launcher\Release\SporeMP.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    native_tests = 'NOT_RUN'
    note = 'Dependencies fetched at the locked commit into an empty export. Rebuild reproducibility, not byte-identical binary reproducibility.'
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destinationPath 'clean-build-result.json') -Encoding utf8
Write-Host "Clean build/test evidence: $destinationPath\clean-build-result.json"
