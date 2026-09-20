# Worker supervisor standard-handle detachment

Scope: Windows HOST process-lifetime regression. No game was launched or controlled by this audit.

`tools/native/start-worker.ps1` now launches the supervisor with Windows `Start-Process` and explicit native files for all three standard handles. Empty `supervisor-stdin.txt` supplies EOF; `supervisor-stdout.log` and `supervisor-stderr.log` retain diagnostic output independently of the short-lived launch script. Credentialed profile loading remains enabled. `-UseNewEnvironment` keeps the environment pointer null in the credentialed launch path, allowing Windows to construct the worker account's environment. The helper quotes each Windows argument before Start-Process joins them.

This choice was checked against the installed PowerShell version's [v7.6.5 Process.cs](https://github.com/PowerShell/PowerShell/blob/v7.6.5/src/Microsoft.PowerShell.Commands.Management/commands/management/Process.cs): lines 2181–2195 open inheritable native files, 2244–2300 assign all three STARTUPINFO handles, and 2356–2386 pass the credential and environment to CreateProcessWithLogonW. Microsoft's [CreateProcessWithLogonW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createprocesswithlogonw) defines the null environment pointer as selecting the specified account's profile environment.

The exact implemented helper and Assert-LocalPath are extracted from their PowerShell AST into the fixture; the production start script is not executed. A .NET Process with captured stdout/stderr starts Python, which starts a second captured Python process, which starts a captured PowerShell process, which calls the helper with Worker 01's existing credential. The last child is Windows PowerShell running a harmless 15-second script. No native game executable, game profile save, current worker metadata, or active game process is touched. The temporary fixture directory is under `C:\ProgramData\SporeMP\M04\01\host-detachment` with Modify access for the existing Worker 01 SID.

Executed command:

```powershell
pwsh -NoProfile -File evidence/2026-09-13-m04-completion/check-detached-launch-host.ps1 -OutputDirectory evidence/2026-09-13-m04-completion/detached-launch-host-dotnet
```

Expected: captured launch returns before the child exits; all capture pipes reach EOF; child has worker SID/name and profile environment; stdin is EOF; output and error files stay writable after all launch intermediates exit; spaces, trailing backslashes, embedded quotes, backslash-plus-quote, and empty arguments retain their exact values.

Observed: **HOST PASS, exit 0**. The .NET outer capture returned after **0.4923813 seconds** and the inner captured chain after **0.406 seconds**, while child PID 38016 remained alive. After its 15-second delay, both stdout and stderr contained their final markers. Identity was `SPOREMP-DEV-PC\SporeMP-M04-01`; USERPROFILE was `C:\Users\SporeMP-M04-01`; APPDATA was `C:\Users\SporeMP-M04-01\AppData\Roaming`; stdin was empty. Argument comparison passed. The child's execution-policy override applies only to this HOST fixture process; no user or machine policy was changed.

Exact command arrays, results, tested source hash, profile/argument observations, and copied logs are in `detached-launch-host-dotnet/`. `detached-launch-provenance.json` seals the harness, production source, reports, logs, and runtime identity.

The initial two fixture attempts exited 1 because Windows PowerShell refused to execute the temporary script under its default execution policy; the child therefore emitted no identity. Those raw captured launch reports remain in `detached-launch-host/` and `detached-launch-host-debug/`. After fixing only the fixture invocation with a process-local policy argument, the nested Python regression passed (`detached-launch-host-fixed/`), followed by the .NET outer capture above.

The parent initially reported a repeated native launcher hang after this change, then corrected that observation: activating the actual WPF window refreshed stale background accessibility/screenshot state and showed the native ready state with enabled Stop and worker selection controls. That native observation belongs to the parent's acceptance capture. This HOST test does not establish WPF visual acceptance or native gameplay, and no additional speculative handle changes were made.

Separate reviewed risk: `LauncherClient.RunAsync` registers the window-lifetime cancellation token and uses `Kill(entireProcessTree: true)` both on cancellation and in finally. If the window closes while a worker mutation backend is still alive, a newly launched supervisor can be its descendant. Worker start/stop should finish their short, persistent mutation independently of window lifetime; explicit worker shutdown should remain the IPC operation. No launcher code was modified by this audit.
