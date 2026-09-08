using SporeMP.Launcher;
using System.Text.Json;
using System.Diagnostics;

int checks = 0;
void Check(bool value, string name) { if (!value) throw new Exception(name); Console.WriteLine("PASS " + name); checks++; }
var client = new LauncherClient(Environment.CurrentDirectory, Environment.ProcessPath!);
string special = "C:\\Game folder & literal $(value)\\\"quote";
var start = client.CreateStartInfo("check", special);
Check(!start.UseShellExecute && start.CreateNoWindow, "no shell or console window");
Check(start.ArgumentList.Count == 4 && start.ArgumentList[3] == special, "path remains one literal argument");
Check(start.RedirectStandardError && start.RedirectStandardOutput, "both pipes captured");
try { client.CreateStartInfo("launch"); throw new Exception("unsafe verb accepted"); } catch (ArgumentException) { checks++; }
string response = JsonSerializer.Serialize(new { schema_version = 1, kind = "sporemp-launcher-operation", native_tests = "NOT_RUN", exit_code = 22, operation = "check", report_path = "report.json", result = new { candidate_match = true, launch_allowed = false } });
var report = LauncherClient.ParseReport(response, 22);
Check(report.ExitCode == 22 && !report.Result.GetProperty("launch_allowed").GetBoolean(), "candidate equality keeps native gate closed");
try { LauncherClient.ParseReport(response, 0); throw new Exception("exit mismatch accepted"); } catch (InvalidDataException) { checks++; }
try { LauncherClient.ParseReport(response.Replace("NOT_RUN", "VERIFIED"), 22); throw new Exception("unqualified evidence accepted"); } catch (InvalidDataException) { checks++; }
var nativeStart = client.CreateStartInfo("native_launch", special);
Check(nativeStart.ArgumentList[1] == "native_launch" && nativeStart.ArgumentList[3] == special, "explicit native action preserves literal path");
string nativeResponse = JsonSerializer.Serialize(new { schema_version = 1, kind = "sporemp-launcher-operation", native_tests = "LIFECYCLE_PASSED", exit_code = 0, operation = "native_launch", report_path = "native.json", result = new { clean_lifecycle = true } });
Check(LauncherClient.ParseReport(nativeResponse, 0).Result.GetProperty("clean_lifecycle").GetBoolean(), "explicit lifecycle evidence accepted");
try { LauncherClient.ParseReport(nativeResponse.Replace("native_launch", "prepare"), 0); throw new Exception("preparation invented native pass"); } catch (InvalidDataException) { checks++; }
try { LauncherClient.ParseReport(nativeResponse.Replace("true", "false"), 0); throw new Exception("unclean lifecycle accepted"); } catch (InvalidDataException) { checks++; }
if (args.Length == 1)
{
    var configured = LauncherClient.FromConfiguration(args[0]);
    string temporary = Path.Combine(Path.GetTempPath(), "sporemp-client-fixture-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(Path.Combine(temporary, "tools", "launcher"));
    try
    {
        await File.WriteAllTextAsync(Path.Combine(temporary, "tools", "launcher", "launcher_service.py"), """
import json, os, pathlib, sys, time
pathlib.Path('fixture-pid.txt').write_text(str(os.getpid()))
action = sys.argv[1]
if action == 'backup':
    print('{malformed', flush=True)
    time.sleep(30)
elif action == 'workspace':
    time.sleep(30)
else:
    print(json.dumps({'kind':'sporemp-launcher-progress','phase':'verify','message':'Checking fixture'}), flush=True)
    print(json.dumps({'schema_version':1,'kind':'sporemp-launcher-operation','native_tests':'NOT_RUN','operation':action,'exit_code':22,'report_path':'fixture.json','result':{'launch_allowed':False}}), flush=True)
    sys.exit(22)
""");
        var live = new LauncherClient(temporary, configured.PythonExecutable);
        var progress = new ImmediateProgress();
        var liveReport = await live.RunAsync("check", progress: progress);
        Check(progress.Phases.SequenceEqual(new[] { "verify" }) && liveReport.ExitCode == 22, "actual child streams progress and preserves gate exit");
        try { await live.RunAsync("backup"); throw new Exception("malformed output accepted"); } catch (JsonException) { }
        bool StillRunning(int pid)
        {
            try { using var process = Process.GetProcessById(pid); return !process.WaitForExit(2000); }
            catch (ArgumentException) { return false; }
        }
        Check(!StillRunning(int.Parse(await File.ReadAllTextAsync(Path.Combine(temporary, "fixture-pid.txt")))), "malformed response does not orphan the child");
        using var cancel = new CancellationTokenSource(300);
        try { await live.RunAsync("workspace", cancellationToken: cancel.Token); throw new Exception("cancellation ignored"); } catch (OperationCanceledException) { }
        Check(!StillRunning(int.Parse(await File.ReadAllTextAsync(Path.Combine(temporary, "fixture-pid.txt")))), "cancellation terminates only the owned diagnostic child");
    }
    finally { Directory.Delete(temporary, recursive: true); }
}
Console.WriteLine($"{checks} launcher host assertions passed; native tests NOT RUN.");

sealed class ImmediateProgress : IProgress<LauncherProgress>
{
    public List<string> Phases { get; } = [];
    public void Report(LauncherProgress progress) => Phases.Add(progress.Phase);
}
