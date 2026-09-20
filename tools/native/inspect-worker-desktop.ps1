param([Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{32}$')][string]$Generation,
      [Parameter(Mandatory)][int]$GamePid)
$ErrorActionPreference='Stop'
# Read-only window inventory on the exact private worker desktop. No input,
# desktop switch, capture, activation, or message is sent to a window.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SporeMPWorkerWindows {
    delegate bool Visit(IntPtr window, IntPtr value);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr OpenDesktop(string name,uint flags,bool inherit,uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop,Visit callback,IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,Visit callback,IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder text,int size);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int size);
    public static string[] Inspect(string generation,uint gamePid) {
        var desktop=OpenDesktop("SporeMP-M04-"+generation,0,false,0x41);
        if(desktop==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        var result=new List<string>();
        Visit add=(window,data)=>{
            uint pid; GetWindowThreadProcessId(window,out pid);
            if(pid==gamePid){
                var text=new StringBuilder(2048); var cls=new StringBuilder(256);
                GetWindowText(window,text,text.Capacity);GetClassName(window,cls,cls.Capacity);
                result.Add(cls+": "+text);
            }
            return true;
        };
        try { EnumDesktopWindows(desktop,(window,data)=>{add(window,data);EnumChildWindows(window,add,IntPtr.Zero);return true;},IntPtr.Zero); }
        finally {CloseDesktop(desktop);}
        return result.ToArray();
    }
}
'@
[SporeMPWorkerWindows]::Inspect($Generation,$GamePid)
