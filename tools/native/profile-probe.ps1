param([Parameter(Mandatory)][string]$Output, [Parameter(Mandatory)][string]$PersonalRoot, [Parameter(Mandatory)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SporeMPAccessProbe {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern IntPtr CreateFile(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  public static int OpenOnly(string path, uint access) {
    var h=CreateFile(path,access,7,IntPtr.Zero,3,0x02000000,IntPtr.Zero);
    if(h==new IntPtr(-1)) return Marshal.GetLastWin32Error();
    CloseHandle(h); return 0;
  }
}
'@
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$folders = [ordered]@{}
foreach ($name in @('UserProfile','ApplicationData','LocalApplicationData','MyDocuments')) {
    $folders[$name] = [Environment]::GetFolderPath([Environment+SpecialFolder]::$name)
}
$paths = @($PersonalRoot, (Join-Path $PersonalRoot 'AppData\Roaming'), (Join-Path $PersonalRoot 'AppData\Roaming\Spore'), (Join-Path $PersonalRoot 'Documents'), (Join-Path $PersonalRoot 'Documents\My Spore Creations'), $GameRoot, (Join-Path $GameRoot 'SporebinEP1\SporeApp.exe'))
$access = foreach ($path in $paths) {
    # OPEN_EXISTING requests rights without changing, creating, or truncating anything.
    [ordered]@{path=$path; write_error=[SporeMPAccessProbe]::OpenOnly($path,0x40000000); delete_error=[SporeMPAccessProbe]::OpenOnly($path,0x10000)}
}
$isolated = $identity.Name.EndsWith('\SporeMP-M01') -and -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
foreach ($folder in $folders.Values) { $isolated = $isolated -and $folder.StartsWith($folders.UserProfile, [StringComparison]::OrdinalIgnoreCase) -and -not $folder.StartsWith($PersonalRoot, [StringComparison]::OrdinalIgnoreCase) }
foreach ($item in $access) { $isolated = $isolated -and $item.write_error -eq 5 -and $item.delete_error -eq 5 }
$result = [ordered]@{schema_version=1;kind='disposable-os-profile-access-probe';utc=(Get-Date).ToUniversalTime().ToString('o');user=$identity.Name;sid=$identity.User.Value;admin=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);shell_folders=$folders;access_probes=$access;os_isolation_gate=$isolated;native_tests='NOT_RUN'}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
if (-not $isolated) { exit 21 }
