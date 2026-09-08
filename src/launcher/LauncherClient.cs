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

    public ProcessStartInfo CreateStartInfo(string action, string? gameRoot = null, string? reportPath = null)
    {
        if (action is not ("prepare" or "check" or "backup" or "workspace" or "export" or "native_launch"))
            throw new ArgumentException("Unsupported launcher operation.");
        var info = new ProcessStartInfo(PythonExecutable)
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true,
            RedirectStandardError = true, WorkingDirectory = RepoRoot
        };
        info.ArgumentList.Add(Path.Combine(RepoRoot, "tools", "launcher", "launcher_service.py"));
        info.ArgumentList.Add(action);
        if (gameRoot is not null) { info.ArgumentList.Add("--game-root"); info.ArgumentList.Add(gameRoot); }
        if (reportPath is not null) { info.ArgumentList.Add("--report"); info.ArgumentList.Add(reportPath); }
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
            native != "NOT_RUN" && operation != "native_launch" ||
            native == "LIFECYCLE_PASSED" && (processExit != 0 || !root.GetProperty("result").GetProperty("clean_lifecycle").GetBoolean()) ||
            root.GetProperty("exit_code").GetInt32() != processExit)
            throw new InvalidDataException("Invalid or inconsistent diagnostic response.");
        return new OperationReport(root.GetProperty("operation").GetString()!, processExit,
            root.GetProperty("report_path").GetString()!, root.GetProperty("result").Clone());
    }

    public async Task<OperationReport> RunAsync(string action, string? gameRoot = null, string? reportPath = null,
        IProgress<LauncherProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        using var process = new Process { StartInfo = CreateStartInfo(action, gameRoot, reportPath) };
        process.StartInfo.ArgumentList.Add("--progress");
        if (!process.Start()) throw new IOException("Could not start the local diagnostics tool.");
        using var registration = cancellationToken.Register(() =>
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
        });
        try
        {
            // Streaming progress and stderr drain concurrently; no shell or UI-thread hashing.
            var stderr = process.StandardError.ReadToEndAsync();
            string? output = null;
            while (await process.StandardOutput.ReadLineAsync(cancellationToken) is string line)
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
            await process.WaitForExitAsync();
            cancellationToken.ThrowIfCancellationRequested();
            string error = await stderr;
            if (string.IsNullOrWhiteSpace(output))
                throw new IOException("Diagnostic tool failed (exit " + process.ExitCode + "): " + error);
            var report = ParseReport(output, process.ExitCode);
            if (report.Operation != action) throw new InvalidDataException("Diagnostic operation does not match the request.");
            return report;
        }
        finally
        {
            // Malformed output or a closed window must not orphan a preparation process.
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
        }
    }
}
