using SporeMP.Launcher;
using System.Text.Json;
using System.Diagnostics;

// A separate HOST fixture process exits while its worker backend is running.
// It never imports or invokes the actual game service.
if (args.Length == 3 && args[0] == "--close-worker-fixture")
{
    var closingClient = new LauncherClient(args[1], args[2]);
    _ = closingClient.RunAsync("worker_start", workerId: "02");
    var deadline = Stopwatch.StartNew();
    while (!File.Exists(Path.Combine(args[1], "worker-mutation-entered.txt")))
    {
        if (deadline.Elapsed > TimeSpan.FromSeconds(5)) throw new Exception("Worker fixture did not start.");
        await Task.Delay(20);
    }
    return;
}

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
var displayStart = client.CreateStartInfo("save_display", display: new("windowed", "1920x1080 -safe"));
Check(displayStart.ArgumentList.Skip(2).SequenceEqual(new[] { "--display-mode", "windowed", "--resolution", "1920x1080 -safe" }), "display values remain separate literal arguments for backend validation");
try { client.CreateStartInfo("native_launch", display: new("windowed", "1280x720")); throw new Exception("display on wrong operation accepted"); } catch (ArgumentException) { checks++; }
try { client.CreateStartInfo("save_display"); throw new Exception("missing display accepted"); } catch (ArgumentException) { checks++; }
var workerStart = client.CreateStartInfo("worker_start", workerId: "02");
Check(workerStart.ArgumentList.Skip(2).SequenceEqual(new[] { "--worker-id", "02" }), "worker selection uses an allowlisted literal argument");
try { client.CreateStartInfo("worker_start", workerId: "02 --play"); throw new Exception("worker injection accepted"); } catch (ArgumentException) { checks++; }
try { client.CreateStartInfo("worker_stop"); throw new Exception("missing worker accepted"); } catch (ArgumentException) { checks++; }
string invite = "sporemp://join?host=127.0.0.1&port=27060&cert=" + new string('a', 64) + "&token=" + new string('b', 64);
Check(ServerInvitation.Parse(invite).Endpoint == "127.0.0.1:27060", "private invitation exposes only its endpoint");
foreach (string invalid in new[] { invite + "&token=" + new string('b', 64), invite + "#fragment", invite.Replace("27060", "65536"), invite.Replace("127.0.0.1", "host%0Arole=authority"), invite.Replace("127.0.0.1", "-bad.host"), invite.Replace(new string('b', 64), new string('0', 64)), "https://example.com/" })
{
    try { ServerInvitation.Parse(invalid); throw new Exception("invalid invitation accepted"); }
    catch (ArgumentException error) { Check(!error.Message.Contains(new string('b', 64)), "malformed invitation rejects without exposing its credential"); }
}
var joinStart = client.CreateStartInfo("native_join", special);
Check(joinStart.RedirectStandardInput && !joinStart.ArgumentList.Any(arg => arg.Contains(new string('b', 64))), "join uses a private stdin pipe with no credential argument");
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
if action == 'native_join':
    invitation = sys.stdin.read(2049)
    assert invitation.startswith('sporemp://join?') and '&token=' in invitation
    assert all('&token=' not in arg for arg in sys.argv)
    with pathlib.Path('join-count.txt').open('a') as log: log.write('join\n')
    pathlib.Path('join-entered.txt').write_text('entered')
    print(json.dumps({'kind':'sporemp-launcher-progress','phase':'connected','message':'Connected fixture'}), flush=True)
    time.sleep(.6)
    print(json.dumps({'schema_version':1,'kind':'sporemp-launcher-operation','native_tests':'NOT_RUN','operation':action,'exit_code':0,'report_path':'fixture.json','result':{'authenticated':True,'connected_baseline_observed':True}}), flush=True)
    sys.exit(0)
if action in ('worker_list', 'worker_start', 'worker_stop'):
    if action == 'worker_list' and pathlib.Path('worker-list-gate.txt').exists():
        pathlib.Path('worker-list-entered.txt').write_text('entered')
        deadline = time.monotonic() + 5
        while pathlib.Path('worker-list-gate.txt').exists():
            if time.monotonic() > deadline: raise RuntimeError('fixture gate timed out')
            time.sleep(.02)
    saved = pathlib.Path('worker-fixture.json')
    state = saved.read_text() if saved.exists() else 'stopped'
    if action != 'worker_list':
        assert sys.argv[sys.argv.index('--worker-id')+1] == '02'
        with pathlib.Path('worker-mutations.txt').open('a') as log: log.write(action + '\n')
        pathlib.Path('worker-mutation-entered.txt').write_text('entered')
        if pathlib.Path('worker-malformed.txt').exists(): print('{malformed', flush=True)
        if pathlib.Path('worker-delay.txt').exists(): time.sleep(.6)
        state = 'starting' if action == 'worker_start' else 'stopped'
        saved.write_text(state)
        pathlib.Path('worker-mutation-completed.txt').write_text(state)
    rows = [{'id':'01','label':'Worker 01','state':'stopped','detail':'fixture','can_start':True,'can_stop':False},
            {'id':'02','label':'Worker 02','state':state,'detail':'fixture','can_start':state in ('stopped','crashed'),'can_stop':state == 'starting'}]
    print(json.dumps({'schema_version':1,'kind':'sporemp-launcher-operation','native_tests':'NOT_RUN','operation':action,'exit_code':0,'report_path':'fixture.json','result':{'workers':rows}}), flush=True)
    sys.exit(0)
if action in ('display_settings', 'save_display'):
    saved = pathlib.Path('display-fixture.json')
    choices = json.loads(saved.read_text()) if saved.exists() else {'mode':'game','resolution':'desktop'}
    if action == 'save_display':
        choices = {'mode':sys.argv[sys.argv.index('--display-mode')+1], 'resolution':sys.argv[sys.argv.index('--resolution')+1]}
        saved.write_text(json.dumps(choices))
    print(json.dumps({'schema_version':1,'kind':'sporemp-launcher-operation','native_tests':'NOT_RUN','operation':action,'exit_code':0,'report_path':'fixture.json','result':{'display':choices,'desktop':'2560x1440','resolutions':['2560x1440','1920x1080','1280x720'],'warning':''}}), flush=True)
    sys.exit(0)
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
        var multiplayer = new MultiplayerSessionModel(live) { Invitation = invite };
        Check(multiplayer.CanJoin && multiplayer.ButtonLabel == "Join server", "valid invitation enables initial Join");
        var joining = multiplayer.JoinAsync(special);
        var duplicateJoin = multiplayer.JoinAsync(special);
        multiplayer.Invitation = "cannot retarget an active join";
        Check(multiplayer.IsActive && !multiplayer.CanJoin && duplicateJoin.IsCompleted && multiplayer.Invitation == invite, "active join suppresses duplicate requests and retains captured invitation");
        var connectionDeadline = Stopwatch.StartNew();
        while (!multiplayer.IsConnected && !joining.IsCompleted && connectionDeadline.Elapsed < TimeSpan.FromSeconds(5)) await Task.Delay(20);
        Check(multiplayer.IsConnected && multiplayer.ButtonLabel == "Connected", "connected native-state progress updates the session model");
        await joining;
        Check(!multiplayer.IsActive && !multiplayer.IsConnected && multiplayer.ButtonLabel == "Rejoin server", "closed session enables Rejoin with the same in-memory identity");
        await multiplayer.JoinAsync(special);
        Check(File.ReadAllLines(Path.Combine(temporary, "join-count.txt")).Length == 2, "two explicit joins execute once each through real stdin child transport");
        multiplayer.Invitation = invite.Replace("127.0.0.1", "localhost");
        Check(multiplayer.ButtonLabel == "Join server", "changing server invitation cannot claim the prior rejoin identity");
        string WorkerFile(string name) => Path.Combine(temporary, name);
        async Task WaitForWorkerFile(string name)
        {
            var deadline = Stopwatch.StartNew();
            while (!File.Exists(WorkerFile(name)))
            {
                if (deadline.Elapsed > TimeSpan.FromSeconds(5)) throw new Exception("Missing worker fixture marker: " + name);
                await Task.Delay(20);
            }
        }
        void ClearMutationMarkers()
        {
            File.Delete(WorkerFile("worker-mutation-entered.txt"));
            File.Delete(WorkerFile("worker-mutation-completed.txt"));
        }
        var workerModel = new WorkerSettingsModel(live);
        await workerModel.LoadAsync();
        Check(workerModel.Workers.Count == 2 && workerModel.CanStart && !workerModel.CanStop, "worker roster reflects prepared stopped workers");
        workerModel.Selected = workerModel.Workers.Single(x => x.Id == "02");
        await workerModel.StartAsync();
        Check(workerModel.Selected?.Id == "02" && !workerModel.CanStart && workerModel.CanStop && workerModel.Status.Contains("Starting"), "real child start transport retains selected worker and disables duplicate start");
        await workerModel.LoadAsync();
        Check(workerModel.Selected?.Id == "02" && workerModel.CanStop, "status refresh preserves worker selection");
        await workerModel.StopAsync();
        Check(workerModel.CanStart && !workerModel.CanStop && workerModel.Status == "Stopped", "real child stop transport updates controls");
        await File.WriteAllTextAsync(Path.Combine(temporary, "worker-fixture.json"), "crashed");
        await workerModel.LoadAsync();
        Check(workerModel.Status == "Worker crashed" && workerModel.CanStart, "crash state remains distinct from stopped and permits a new generation");
        var originalChoices = workerModel.Workers.ToArray();
        int rosterChanges = 0;
        workerModel.Workers.CollectionChanged += (_, _) => rosterChanges++;
        await File.WriteAllTextAsync(WorkerFile("worker-list-gate.txt"), "hold read");
        var polling = workerModel.LoadAsync();
        await WaitForWorkerFile("worker-list-entered.txt");
        Check(workerModel.CanSelect && workerModel.CanStart && !workerModel.CanRefresh, "background poll leaves selection and explicit worker actions enabled");
        workerModel.Selected = originalChoices.Single(choice => choice.Id == "01");
        File.Delete(WorkerFile("worker-list-gate.txt"));
        await polling;
        Check(ReferenceEquals(workerModel.Selected, originalChoices[0]) && rosterChanges == 0 && workerModel.Workers.Zip(originalChoices).All(pair => ReferenceEquals(pair.First, pair.Second)), "completed poll preserves latest selection and existing dropdown item instances without collection reset");

        workerModel.Selected = originalChoices.Single(choice => choice.Id == "02");
        File.Delete(WorkerFile("worker-list-entered.txt"));
        ClearMutationMarkers();
        await File.WriteAllTextAsync(WorkerFile("worker-list-gate.txt"), "hold read");
        polling = workerModel.LoadAsync();
        await WaitForWorkerFile("worker-list-entered.txt");
        int earlierMutations = File.ReadAllLines(WorkerFile("worker-mutations.txt")).Length;
        using var closingWindow = new CancellationTokenSource();
        var queuedStart = workerModel.StartAsync(closingWindow.Token);
        var duplicateStart = workerModel.StartAsync();
        closingWindow.Cancel();
        Check(workerModel.IsMutating && !workerModel.CanSelect && !File.Exists(WorkerFile("worker-mutation-entered.txt")) && duplicateStart.IsCompleted, "explicit start waits for poll completion and suppresses a duplicate mutation");
        File.Delete(WorkerFile("worker-list-gate.txt"));
        await polling;
        await workerModel.PendingMutation;
        Check(queuedStart.IsCompleted && workerModel.CanStop && !workerModel.IsMutating && File.ReadAllLines(WorkerFile("worker-mutations.txt")).Length == earlierMutations + 1, "queued explicit start survives window cancellation and executes once");

        ClearMutationMarkers();
        await File.WriteAllTextAsync(WorkerFile("worker-delay.txt"), "delay backend");
        using var workerCancel = new CancellationTokenSource();
        var detachedStop = live.RunAsync("worker_stop", workerId: "02", cancellationToken: workerCancel.Token);
        await WaitForWorkerFile("worker-mutation-entered.txt");
        workerCancel.Cancel();
        var detachedStopReport = await detachedStop;
        Check(detachedStopReport.ExitCode == 0 && File.ReadAllText(WorkerFile("worker-mutation-completed.txt")) == "stopped", "window cancellation does not kill or abandon an already started worker stop backend");

        ClearMutationMarkers();
        using var closedBeforeStart = new CancellationTokenSource();
        closedBeforeStart.Cancel();
        try { await live.RunAsync("worker_start", workerId: "02", cancellationToken: closedBeforeStart.Token); throw new Exception("pre-cancelled mutation started"); } catch (OperationCanceledException) { }
        Check(!File.Exists(WorkerFile("worker-mutation-entered.txt")), "cancelled window cannot initiate a new worker backend");

        var closingStart = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true, RedirectStandardOutput = true };
        closingStart.ArgumentList.Add("--close-worker-fixture");
        closingStart.ArgumentList.Add(temporary);
        closingStart.ArgumentList.Add(configured.PythonExecutable);
        using (var closingProcess = Process.Start(closingStart)!)
        {
            var outputDrain = closingProcess.StandardOutput.ReadToEndAsync();
            var errorDrain = closingProcess.StandardError.ReadToEndAsync();
            await closingProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
            Check(closingProcess.ExitCode == 0, "fixture launcher process closes while worker start remains in flight: " + await errorDrain);
            await outputDrain;
        }
        await WaitForWorkerFile("worker-mutation-completed.txt");
        Check(File.ReadAllText(WorkerFile("worker-mutation-completed.txt")) == "starting", "worker start backend completes after its launcher process has exited");

        ClearMutationMarkers();
        await File.WriteAllTextAsync(WorkerFile("worker-malformed.txt"), "emit malformed early output");
        try { await live.RunAsync("worker_stop", workerId: "02"); throw new Exception("malformed worker report accepted"); } catch (JsonException) { }
        await WaitForWorkerFile("worker-mutation-completed.txt");
        Check(File.ReadAllText(WorkerFile("worker-mutation-completed.txt")) == "stopped", "malformed worker output cannot trigger recursive backend or authority termination");
        File.Delete(WorkerFile("worker-malformed.txt"));
        File.Delete(WorkerFile("worker-delay.txt"));
        var displayModel = new DisplaySettingsModel(live);
        await displayModel.LoadAsync();
        Check(displayModel.SelectedMode?.Value == "game" && !displayModel.CanChooseResolution && !displayModel.CanSave, "first display load retains game settings");
        displayModel.SelectedMode = displayModel.Modes.Single(x => x.Value == "windowed");
        displayModel.SelectedResolution = displayModel.Resolutions.Single(x => x.Value == "1920x1080");
        Check(displayModel.CanChooseResolution && displayModel.CanSave && displayModel.Message.Contains("Unsaved"), "changing display enables save and exposes pending state");
        await displayModel.SaveAsync();
        Check(!displayModel.CanSave && displayModel.Message.StartsWith("Saved.") && displayModel.Summary == "Windowed · 1920 × 1080", "successful child save updates display summary");
        var reopenedDisplay = new DisplaySettingsModel(live);
        await reopenedDisplay.LoadAsync();
        Check(reopenedDisplay.SelectedMode?.Value == "windowed" && reopenedDisplay.SelectedResolution?.Value == "1920x1080", "reopening restores display choices from child persistence");
        reopenedDisplay.SelectedMode = reopenedDisplay.Modes.Single(x => x.Value == "game");
        await reopenedDisplay.SaveAsync();
        Check(!reopenedDisplay.CanChooseResolution && reopenedDisplay.SelectedResolution?.Value == "desktop", "use game settings clears display override");
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
