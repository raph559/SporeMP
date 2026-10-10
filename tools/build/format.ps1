# Formats the project's C++ (src/ and tests/: .cpp, .h, .inc) with the repository's .clang-format.
# -Check changes nothing and exits 1 if a file is not formatted. -Files limits it to the files named.
param([switch]$Check, [string[]]$Files)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')

function Find-ClangFormat {
    $onPath = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        foreach ($install in & $vswhere -products * -latest -property installationPath) {
            $candidate = Join-Path $install 'VC\Tools\Llvm\x64\bin\clang-format.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'clang-format was not found on PATH or in a Visual Studio installation (component: C++ Clang tools).'
}

$clangFormat = Find-ClangFormat
Push-Location $root
try {
    # A list given through `pwsh -File` arrives as one comma-separated string.
    $files = if ($Files) { @($Files | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) } else { @(git ls-files -- 'src/*.cpp' 'src/*.h' 'src/*.inc' 'tests/*.cpp' 'tests/*.h' 'tests/*.inc') }
    if (-not $files) { throw 'No C++ files found.' }
    $arguments = @('--style=file')
    $arguments += if ($Check) { @('--dry-run', '--Werror') } else { @('-i') }
    $failed = $false
    # In batches: a Windows command line holds about 32,000 characters.
    for ($i = 0; $i -lt $files.Count; $i += 40) {
        $batch = $files[$i..([Math]::Min($i + 39, $files.Count - 1))]
        & $clangFormat @arguments @batch
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    Write-Host ("{0} {1} files with {2}" -f $(if ($Check) { 'Checked' } else { 'Formatted' }), $files.Count, (& $clangFormat --version))
    if ($failed) { exit 1 }
}
finally { Pop-Location }
