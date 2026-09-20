[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolRoot = Join-Path $repoRoot 'external/re-tools'
New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
# Portable developer tools only. Pins are official GitHub release-asset SHA-256
# digests retrieved on 2026-09-12; this does not modify PATH or install services.
$packages = @(
    @{
        Name = 'ghidra_12.1.3_PUBLIC_20260817.zip'
        Root = 'ghidra_12.1.3_PUBLIC'
        Url = 'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.3_build/ghidra_12.1.3_PUBLIC_20260817.zip'
        Sha256 = '93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54'
    },
    @{
        Name = 'OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip'
        Root = 'jdk-21.0.12.1+1'
        Url = 'https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip'
        Sha256 = 'f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e'
    }
)
$results = @()
foreach ($package in $packages) {
    $archive = Join-Path $toolRoot $package.Name
    $expanded = Join-Path $toolRoot $package.Root
    $marker = Join-Path $toolRoot ($package.Name + '.verified.json')
    if (-not (Test-Path -LiteralPath $archive)) {
        & curl.exe --fail --location --silent --show-error --retry 2 --output $archive $package.Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed for $($package.Name), exit $LASTEXITCODE" }
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $package.Sha256) { throw "SHA-256 mismatch: $archive" }
    if (-not (Test-Path -LiteralPath $marker)) {
        if (Test-Path -LiteralPath $expanded) { throw "Unmarked tool directory exists: $expanded" }
        Expand-Archive -LiteralPath $archive -DestinationPath $toolRoot
        if (-not (Test-Path -LiteralPath $expanded -PathType Container)) { throw "Expected package root missing: $expanded" }
        [ordered]@{url=$package.Url;sha256=$actual;directory=$expanded;utc=(Get-Date).ToUniversalTime().ToString('o')} |
            ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8NoBOM
    }
    $results += [ordered]@{name=$package.Name;sha256=$actual;directory=$expanded}
}
$results | ConvertTo-Json -Depth 4
