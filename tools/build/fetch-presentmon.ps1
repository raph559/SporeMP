[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
# Standalone diagnostic executable. No installation or service modification.
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$destination = Join-Path $repoRoot 'external/PresentMon-2.5.1/PresentMon-2.5.1-x64.exe'
$expected = '9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191'
$url = 'https://github.com/GameTechDev/PresentMon/releases/download/v2.5.1/PresentMon-2.5.1-x64.exe'
if (-not (Test-Path -LiteralPath $destination)) {
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Invoke-WebRequest -Uri $url -OutFile $destination
}
if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Pinned PresentMon SHA-256 mismatch. Do not execute this file.'
}
Write-Output $destination
