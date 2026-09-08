[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputPath, [string]$GameRoot = 'C:\Games\SPORE')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (Test-Path -LiteralPath $OutputPath) { throw 'Use a new evidence path; refusing to overwrite.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = if (Test-Path -LiteralPath $vswhere) { (& $vswhere -all -products '*' -format json | ConvertFrom-Json) | Select-Object installationPath,installationVersion,displayName } else { @() }
$executables = foreach ($relative in @('SporeBin\SporeApp.exe','SporebinEP1\SporeApp.exe')) {
    $file = Join-Path $GameRoot $relative
    if (Test-Path -LiteralPath $file) {
        $info = Get-Item -LiteralPath $file
        [ordered]@{ path = $file; bytes = $info.Length; file_version = $info.VersionInfo.FileVersion; product_version = $info.VersionInfo.ProductVersion; sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
}
$toolVersions = [ordered]@{}
foreach ($tool in @('git','cmake','python','pwsh')) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if ($command) { $toolVersions[$tool] = [ordered]@{ path = $command.Source; version = (& $tool --version | Select-Object -First 1) } }
}
$report = [ordered]@{
    schema_version = 1
    observed_utc = (Get-Date).ToUniversalTime().ToString('o')
    initial_repository = 'Empty directory; no .git or user files; no parent AGENTS.md found'
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture)
    cpu = (Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    memory_bytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    video = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    visual_studio = @($visualStudio)
    tools = $toolVersions
    executables = @($executables)
    sdk_commit = (& git -C (Join-Path $repoRoot 'external\Spore-ModAPI') rev-parse HEAD)
    launcher_source_commit = (& git -C (Join-Path $repoRoot 'external\ModAPI-Launcher-Kit') rev-parse HEAD)
    running_spore = @(Get-Process -Name SporeApp -ErrorAction SilentlyContinue | Select-Object Id,Path)
    launcher_appdata_exists = (Test-Path -LiteralPath (Join-Path $env:APPDATA 'Spore ModAPI Launcher'))
    native_execution = 'NOT_RUN'
}
$absoluteOutput = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($absoluteOutput)) | Out-Null
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $absoluteOutput -Encoding utf8
Write-Host "Environment evidence: $absoluteOutput"
