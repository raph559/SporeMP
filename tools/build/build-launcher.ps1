[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location $repoRoot
try {
    $expectedSdk = (Get-Content -LiteralPath global.json -Raw | ConvertFrom-Json).sdk.version
    $actualSdk = & dotnet --version
    if ($LASTEXITCODE -ne 0 -or $actualSdk -ne $expectedSdk) { throw "Launcher requires .NET SDK $expectedSdk." }
    $pythonPath = & python -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pythonPath)) { throw 'Python 3.10+ is required for M01 local diagnostics.' }
    $outputRoot = Join-Path $repoRoot "build\launcher\$Configuration"
    Write-Host "> dotnet build src/launcher/SporeMP.Launcher.csproj -c $Configuration -o $outputRoot --nologo"
    & dotnet build src/launcher/SporeMP.Launcher.csproj -c $Configuration -o $outputRoot --nologo
    if ($LASTEXITCODE -ne 0) { throw "Launcher build failed: $LASTEXITCODE" }
    @{ repo_root = $repoRoot; python_executable = $pythonPath } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'launcher.runtime.json') -Encoding utf8NoBOM
    Write-Host "> dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c $Configuration --nologo"
    & dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c $Configuration -o (Join-Path $repoRoot "build\launcher-tests\$Configuration") --nologo
    if ($LASTEXITCODE -ne 0) { throw "Launcher host test build failed: $LASTEXITCODE" }
    Write-Host "Launcher built: $outputRoot\SporeMP.exe (not started)."
} finally { Pop-Location }
