# Runs clang-tidy with the repository's .clang-tidy over the C++ sources that clang can parse: everything under
# src/network and src/worker, and the bridge sources that include no game SDK header (clang does not parse the SDK's
# EASTL). Exits 1 if there is a finding. -Files limits it to the files named.
param([string[]]$Files)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')

function Find-ClangTidy {
    $onPath = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        foreach ($install in & $vswhere -products * -latest -property installationPath) {
            $candidate = Join-Path $install 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'clang-tidy was not found on PATH or in a Visual Studio installation (component: C++ Clang tools).'
}

$clangTidy = Find-ClangTidy
Push-Location $root
try {
    $sources = if ($Files) { @($Files | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) }
    else {
        @(git ls-files -- 'src/network/*.cpp' 'src/worker/*.cpp') +
        @(git ls-files -- 'src/bridge/*.cpp' | Where-Object {
                -not (Select-String -Path $_ -Pattern '#include <Spore/|#include "native_|build_identity\.h|native_identity\.h|detours\.h' -Quiet)
            })
    }
    $compiler = @('--target=i686-pc-windows-msvc', '-std=c++17', '-fms-compatibility', '-fms-extensions',
        '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0602', '-DSECURITY_WIN32',
        '-Isrc/network', '-Isrc/worker', '-Isrc/bridge')
    $findings = 0
    foreach ($source in $sources) {
        $output = & $clangTidy $source --quiet -- @compiler 2>&1 | Out-String
        $lines = $output -split "`r?`n" | Where-Object { $_ -match '(warning|error):' } | Sort-Object -Unique
        $findings += $lines.Count
        $lines | ForEach-Object { $_ }
    }
    Write-Host ("{0} findings in {1} files" -f $findings, $sources.Count)
    if ($findings) { exit 1 }
}
finally { Pop-Location }
