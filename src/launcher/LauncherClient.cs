using System.Diagnostics;
using System.IO;
using System.Text.Json;

namespace SporeMP.Launcher;

public sealed record OperationReport(string Operation, int ExitCode, string ReportPath, JsonElement Result);
public sealed record LauncherProgress(string Phase, string Message);

public sealed class LauncherClient(string repoRoot, string pythonExecutable)
{
    public string RepoRoot { get; } = Path.GetFullPath(repoRoot);
    public string PythonExecutable { get; } = Path.GetFullPath(pythonExecutable);
    public static LauncherClient FromConfiguration(string path)
    {
        using var config = JsonDocument.Parse(File.ReadAllText(path));
        var client = new LauncherClient(config.RootElement.GetProperty("repo_root").GetString()!,
            config.RootElement.GetProperty("python_executable").GetString()!);
        if (!File.Exists(client.PythonExecutable) || !File.Exists(Path.Combine(client.RepoRoot, "tools", "launcher", "launcher_service.py")))
            throw new FileNotFoundException("The configured source checkout or Python interpreter is missing.");
        return client;
    }

    public ProcessStartInfo CreateStartInfo(string action, string? gameRoot = null, string? reportPath = null, DisplayPreferences? display = null, string? workerId = null)
    {
        if (action is not ("prepare" or "check" or "backup" or "workspace" or "export" or "native_launch" or "native_join" or "display_settings" or "save_display" or "worker_list" or "worker_start" or "worker_stop"))
            throw new ArgumentException("Unsupported launcher operation.");
        if ((action == "save_display") != (display is not null))
            throw new ArgumentException("Display choices are required only when saving display settings.");
        if ((action is "worker_start" or "worker_stop") != (workerId is not null) || workerId is not (null or "01" or "02" or "03"))
            throw new ArgumentException("Select an isolated worker for worker start/stop.");
        var info = new ProcessStartInfo(PythonExecutable)
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true,
            RedirectStandardError = true, RedirectStandardInput = action == "native_join", WorkingDirectory = RepoRoot
        };
        info.ArgumentList.Add(Path.Combine(RepoRoot, "tools", "launcher", "launcher_service.py"));
        info.ArgumentList.Add(action);
        if (gameRoot is not null) { info.ArgumentList.Add("--game-root"); info.ArgumentList.Add(gameRoot); }
        if (reportPath is not null) { info.ArgumentList.Add("--report"); info.ArgumentList.Add(reportPath); }
        if (workerId is not null) { info.ArgumentList.Add("--worker-id"); info.ArgumentList.Add(workerId); }
        if (display is not null)
        {
            info.ArgumentList.Add("--display-mode"); info.ArgumentList.Add(display.Mode);
            info.ArgumentList.Add("--resolution"); info.ArgumentList.Add(display.Resolution);
        }
        info.Environment["PYTHONIOENCODING"] = "utf-8";
        return info;
    }

    public static OperationReport ParseReport(string json, int processExit)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        string? native = root.GetProperty("native_tests").GetString();
        string? operation = root.GetProperty("operation").GetString();
        if (root.GetProperty("schema_version").GetInt32() != 1 ||
            root.GetProperty("kind").GetString() != "sporemp-launcher-operation" ||
            native is not ("NOT_RUN" or "LIFECYCLE_PASSED" or "FAILED") ||
            native != "NOT_RUN" && operation is not ("native_launch" or "native_join") ||
            native == "LIFECYCLE_PASSED" && (processExit != 0 || !root.GetProperty("result").GetProperty("clean_lifecycle").GetBoolean()) ||
            root.GetProperty("exit_code").GetInt32() != processExit)
            throw new InvalidDataException("Invalid or inconsistent diagnostic response.");
        return new OperationReport(root.GetProperty("operation").GetString()!, processExit,
            root.GetProperty("report_path").GetString()!, root.GetProperty("result").Clone());
    }

    public async Task<OperationReport> RunAsync(string action, string? gameRoot = null, string? reportPath = null,
        IProgress<LauncherProgress>? progress = null, CancellationToken cancellationToken = default, DisplayPreferences? display = null, string? workerId = null, string? invitation = null)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if ((action == "native_join") != (invitation is not null))
            throw new ArgumentException("An invitation is required only when joining multiplayer.");
        if (invitation is not null) _ = ServerInvitation.Parse(invitation);
        bool workerMutation = action is "worker_start" or "worker_stop";
        // A submitted worker mutation owns its backend lifetime. Window close
        // cancels read-only diagnostics, but must never kill worker authority.
        using var mutationDeadline = workerMutation ? new CancellationTokenSource(TimeSpan.FromMinutes(3)) : null;
        var readToken = mutationDeadline?.Token ?? cancellationToken;
        using var process = new Process { StartInfo = CreateStartInfo(action, gameRoot, reportPath, display, workerId) };
        process.StartInfo.ArgumentList.Add("--progress");
        if (!process.Start()) throw new IOException("Could not start the local diagnostics tool.");
        using var registration = (workerMutation ? CancellationToken.None : cancellationToken).Register(() =>
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
        });
        try
        {
            if (invitation is not null)
            {
                // The credential never enters argv, environment, preferences or
                // launcher reports. This pipe is private to the owned backend.
                await process.StandardInput.WriteAsync(invitation.AsMemory(), readToken);
                process.StandardInput.Close();
            }
            // Streaming progress and stderr drain concurrently; no shell or UI-thread hashing.
            var stderr = process.StandardError.ReadToEndAsync();
            string? output = null;
            while (await process.StandardOutput.ReadLineAsync(readToken) is string line)
            {
                if (string.IsNullOrWhiteSpace(line)) continue;
                using var message = JsonDocument.Parse(line);
                if (message.RootElement.TryGetProperty("kind", out var kind) && kind.GetString() == "sporemp-launcher-progress")
                    progress?.Report(new(message.RootElement.GetProperty("phase").GetString()!, message.RootElement.GetProperty("message").GetString()!));
                else
                {
                    if (output is not null) throw new InvalidDataException("Diagnostic tool returned multiple final reports.");
                    output = line;
                }
            }
            await process.WaitForExitAsync(readToken);
            readToken.ThrowIfCancellationRequested();
            string error = await stderr.WaitAsync(readToken);
            if (string.IsNullOrWhiteSpace(output))
                throw new IOException("Diagnostic tool failed (exit " + process.ExitCode + "): " + error);
            var report = ParseReport(output, process.ExitCode);
            if (report.Operation != action) throw new InvalidDataException("Diagnostic operation does not match the request.");
            return report;
        }
        catch (OperationCanceledException) when (workerMutation && mutationDeadline!.IsCancellationRequested)
        {
            throw new IOException("Worker operation is still pending or its outcome is unknown. Its backend was left running; refresh status before trying again.");
        }
        finally
        {
            // Worker backends have their own operation deadlines and persist a
            // report before printing it. They continue even if the window or its
            // output pipe disappears. Never kill their supervisor descendants.
            try { if (!workerMutation && !process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
        }
    }
}
