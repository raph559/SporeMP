# Compiles C++ files of this checkout for syntax and warnings only (cl /Zs /W4 /WX, Win32), writing nothing.
# For a quick check of an edit, also from a Git worktree that has no build tree of its own: the SDK and the
# generated headers are taken from the repository's main checkout, which must have been built once.
# usage: check-file.ps1 src/bridge/native_scene.cpp [more files]
param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$main = (git -C $root worktree list --porcelain | Select-String '^worktree ' | Select-Object -First 1).Line.Substring(9)
$sdk = Join-Path $main 'external/Spore-ModAPI'
$generated = Join-Path $main 'build/win32/generated'
if (-not (Test-Path $sdk) -or -not (Test-Path (Join-Path $generated 'build_identity.h'))) {
    throw "The main checkout at $main has no SDK or generated headers; run tools/build/build.ps1 there first."
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path $vcvars)) { throw 'No Visual Studio C++ toolset was found.' }

$include = @('src/bridge', 'src/network', 'src/worker', 'tests/unit') | ForEach-Object { '/I"{0}"' -f (Join-Path $root $_) }
$include += '/I"{0}"' -f $generated
# The launch host includes a per-configuration generated header.
$include += '/I"{0}"' -f (Join-Path $main 'build/win32/native/Release')
$include += '/I"{0}"' -f (Join-Path $main 'external/ModAPI-Launcher-Kit/ModAPI.DLLInjector/Detours/src')
$external = @('Spore ModAPI', 'EASTL-3.02.01/include', 'EASTL-3.02.01/test/packages/EABase/include/Common',
    'EASTL-3.02.01/test/packages/EAAssert/include', 'EASTL-3.02.01/test/packages/EAStdC/include', 'Detours/include') |
    ForEach-Object { '/external:I"{0}"' -f (Join-Path $sdk $_) }
$defines = @('WIN32', '_WINDOWS', 'WIN32_LEAN_AND_MEAN', 'NOMINMAX', 'UNICODE', '_UNICODE', '_WIN32_WINNT=0x0602', 'SECURITY_WIN32', 'NDEBUG') |
    ForEach-Object { "/D$_" }
$options = @('/nologo', '/Zs', '/std:c++17', '/EHsc', '/MD', '/W4', '/WX', '/external:W0') + $defines + $include + $external
$sources = $Files | ForEach-Object { '"{0}"' -f (Resolve-Path (Join-Path $root $_)) }
$command = '"{0}" x86 >nul 2>&1 && cl {1} {2}' -f $vcvars, ($options -join ' '), ($sources -join ' ')
cmd /c $command
exit $LASTEXITCODE
