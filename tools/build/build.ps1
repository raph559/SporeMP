[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$FetchDependencies,
    [switch]$SdkOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'config\dependencies.lock.json') -Raw | ConvertFrom-Json
$sdkRoot = Join-Path $repoRoot 'external\Spore-ModAPI'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Write-Host ('> ' + $Program + ' ' + ($Arguments -join ' '))
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE : $Program" }
}
if (-not (Test-Path -LiteralPath $sdkRoot)) {
    if (-not $FetchDependencies) { throw 'SDK missing. Re-run with -FetchDependencies to fetch the pinned source.' }
    New-Item -ItemType Directory -Path $sdkRoot | Out-Null
    Invoke-Checked 'git' @('-C', $sdkRoot, 'init')
    Invoke-Checked 'git' @('-C', $sdkRoot, 'remote', 'add', 'origin', $lock.modapi.repository)
    Invoke-Checked 'git' @('-C', $sdkRoot, 'fetch', '--depth', '1', 'origin', $lock.modapi.commit)
    Invoke-Checked 'git' @('-C', $sdkRoot, 'checkout', '--detach', 'FETCH_HEAD')
}
$sdkRevision = & git -C $sdkRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $sdkRevision -ne $lock.modapi.commit) { throw "SDK revision mismatch: $sdkRevision" }
$sdkChanges = & git -C $sdkRoot status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $sdkChanges) { throw 'Pinned SDK source has local tracked modifications; preserve and review them before building.' }
$loaderRoot = Join-Path $repoRoot 'external\ModAPI-Launcher-Kit'
if (-not (Test-Path -LiteralPath $loaderRoot)) {
    if (-not $FetchDependencies) { throw 'Pinned loader source missing. Re-run with -FetchDependencies.' }
    New-Item -ItemType Directory -Path $loaderRoot | Out-Null
    Invoke-Checked 'git' @('-C',$loaderRoot,'init')
    Invoke-Checked 'git' @('-C',$loaderRoot,'remote','add','origin',$lock.native_loader.repository)
    Invoke-Checked 'git' @('-C',$loaderRoot,'fetch','--depth','1','origin',$lock.native_loader.commit)
    Invoke-Checked 'git' @('-C',$loaderRoot,'checkout','--detach','FETCH_HEAD')
}
$loaderRevision = & git -C $loaderRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $loaderRevision -ne $lock.native_loader.commit) { throw 'Loader revision differs from lock.' }
$loaderChanges = & git -C $loaderRoot status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $loaderChanges) { throw 'Pinned loader source has tracked modifications.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere missing.' }
$vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2022 C++ x86/x64 tools missing.' }
$compiler = Join-Path $vsRoot ('VC\Tools\MSVC\' + $lock.modapi.msvc_tools_version + '\bin\Hostx64\x86\cl.exe')
if (-not (Test-Path -LiteralPath $compiler)) { throw "Pinned compiler missing: $compiler" }
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
$detoursBuild = Join-Path $repoRoot 'build\detours4'
Invoke-Checked 'cmake' @('-S', (Join-Path $PSScriptRoot 'detours4'), '-B', $detoursBuild, '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', "v143,version=$($lock.modapi.msvc_tools_version)", "-DCMAKE_SYSTEM_VERSION=$($lock.modapi.windows_sdk)")
Invoke-Checked 'cmake' @('--build', $detoursBuild, '--config', $Configuration, '--parallel', '4')
$sdkDetours = @("/p:ForceImportAfterCppTargets=$(Join-Path $PSScriptRoot 'sdk-detours4.targets')", "/p:SporeMPDetoursSource=$(Join-Path $loaderRoot 'ModAPI.DLLInjector\Detours\src')", "/p:SporeMPDetoursLibrary=$(Join-Path $detoursBuild "$Configuration\sporemp_detours.lib")")
$sdkOutput = Join-Path $repoRoot "build\sdk\$Configuration"
New-Item -ItemType Directory -Path $sdkOutput -Force | Out-Null
$sdkProject = Join-Path $sdkRoot 'Spore ModAPI\Spore ModAPI.vcxproj'
Invoke-Checked $msbuild (@($sdkProject, '/t:Build', '/m:2', '/v:minimal', '/nologo', "/p:Configuration=$Configuration DLL", '/p:Platform=Win32', "/p:PlatformToolset=$($lock.modapi.toolset)", "/p:VCToolsVersion=$($lock.modapi.msvc_tools_version)", "/p:WindowsTargetPlatformVersion=$($lock.modapi.windows_sdk)", "/p:SDK_BUILD_VER=$($lock.modapi.sdk_build_ver)", "/p:EXECUTABLE_TYPE=$($lock.modapi.executable_type)", "/p:OutDir=$sdkOutput\", "/p:IntDir=$sdkOutput\obj-detours4\", '/p:CL_MPCount=4', "/bl:$sdkOutput\sdk-build.binlog", '/fl', "/flp:logfile=$sdkOutput\sdk-build.log;verbosity=normal") + $sdkDetours)
$baseOutput = Join-Path $repoRoot "build\sdk-base\$Configuration"
New-Item -ItemType Directory -Path $baseOutput -Force | Out-Null
Invoke-Checked $msbuild (@($sdkProject, '/t:Build', '/m:2', '/v:minimal', '/nologo', "/p:Configuration=$Configuration", '/p:Platform=Win32', "/p:PlatformToolset=$($lock.modapi.toolset)", "/p:VCToolsVersion=$($lock.modapi.msvc_tools_version)", "/p:WindowsTargetPlatformVersion=$($lock.modapi.windows_sdk)", "/p:OutDir=$baseOutput\", "/p:IntDir=$baseOutput\obj-detours4\", '/p:CL_MPCount=4', '/fl', "/flp:logfile=$baseOutput\build.log;verbosity=normal") + $sdkDetours)
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'verify-sdk-detours.py'), '--repo', $repoRoot, '--configuration', $Configuration)
if ($SdkOnly) { return }
$loaderOutput = Join-Path $repoRoot "build\injector\$Configuration"
New-Item -ItemType Directory -Path $loaderOutput -Force | Out-Null
Invoke-Checked $msbuild @((Join-Path $loaderRoot 'ModAPI.DLLInjector\ModAPI.DLLInjector.vcxproj'), '/t:Build', '/m:2', '/v:minimal', '/nologo', "/p:Configuration=$Configuration", '/p:Platform=Win32', "/p:VCToolsVersion=$($lock.modapi.msvc_tools_version)", "/p:WindowsTargetPlatformVersion=$($lock.modapi.windows_sdk)", "/p:OutDir=$loaderOutput\", "/p:IntDir=$loaderOutput\obj\", '/p:CL_MPCount=4', '/fl', "/flp:logfile=$loaderOutput\build.log;verbosity=normal")
$buildRoot = Join-Path $repoRoot 'build\win32'
Invoke-Checked 'cmake' @('-S', $repoRoot, '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', "v143,version=$($lock.modapi.msvc_tools_version)", "-DCMAKE_SYSTEM_VERSION=$($lock.modapi.windows_sdk)")
Invoke-Checked 'cmake' @('--build', $buildRoot, '--config', $Configuration, '--parallel', '4')
Invoke-Checked 'pwsh' @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'build-launcher.ps1'), '-Configuration', $Configuration)
Write-Host "Build complete. Nothing was installed or launched. Artifacts: $buildRoot\$Configuration"
