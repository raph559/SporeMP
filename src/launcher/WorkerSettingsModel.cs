using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;

namespace SporeMP.Launcher;

public sealed class WorkerChoice(string id, string label, string state, string detail, bool canStart, bool canStop) : INotifyPropertyChanged
{
    public string Id { get; } = id;
    public string Label { get; private set; } = label;
    public string State { get; private set; } = state;
    public string Detail { get; private set; } = detail;
    public bool CanStart { get; private set; } = canStart;
    public bool CanStop { get; private set; } = canStop;
    public event PropertyChangedEventHandler? PropertyChanged;
    public void Update(WorkerChoice value)
    {
        if (Id != value.Id) throw new ArgumentException("A worker choice cannot change identity.");
        string[] changed = [.. new[] {
            Label != value.Label ? nameof(Label) : null,
            State != value.State ? nameof(State) : null,
            Detail != value.Detail ? nameof(Detail) : null,
            CanStart != value.CanStart ? nameof(CanStart) : null,
            CanStop != value.CanStop ? nameof(CanStop) : null
        }.OfType<string>()];
        Label = value.Label; State = value.State; Detail = value.Detail; CanStart = value.CanStart; CanStop = value.CanStop;
        foreach (var property in changed) PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(property));
    }
}

public sealed class WorkerSettingsModel(LauncherClient client) : INotifyPropertyChanged
{
    private bool refreshing, mutating;
    private readonly SemaphoreSlim operations = new(1, 1);
    private WorkerChoice? selected;
    public event PropertyChangedEventHandler? PropertyChanged;
    public ObservableCollection<WorkerChoice> Workers { get; } = [];
    public WorkerChoice? Selected
    {
        get => selected;
        set { if (ReferenceEquals(selected, value)) return; selected = value; Refresh(); }
    }
    public bool CanRefresh => !refreshing && !mutating;
    public bool CanStart => !mutating && Selected?.CanStart == true;
    public bool CanStop => !mutating && Selected?.CanStop == true;
    public bool CanSelect => !mutating;
    public bool IsMutating => mutating;
    public Task PendingMutation { get; private set; } = Task.CompletedTask;
    public string Status => Selected?.State switch
    {
        "starting" => "Starting SPORE…", "menu" => "Engine connected · waiting for a scene",
        "loading" => "Loading scene…", "ready" => "Native simulation is updating",
        "simulation_stalled" => "Native simulation is paused or stalled", "stopping" => "Stopping…",
        "stopped" => "Stopped", "stopped_forced" => "Stopped (forced)", "crashed" => "Worker crashed", "bridge_failed" => "Bridge needs attention",
        "unresponsive" => "Engine is not responding", "unavailable" => "Supervisor unavailable", _ => "No worker selected"
    };
    public string Detail => Selected?.Detail ?? "No isolated workers have been prepared for this development build.";
    public string Message { get; private set; } = "Reading local workers…";
    public string DesktopRequirement => "Keep Windows signed in and worker windows open without minimizing them. Minimizing pauses the simulation and can cause the worker to time out.";
    private void Refresh() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));
    public Task LoadAsync(CancellationToken token = default) => refreshing || mutating ? Task.CompletedTask : RunAsync("worker_list", token);
    public Task StartAsync(CancellationToken token = default) => CanStart && !token.IsCancellationRequested ? PendingMutation = RunAsync("worker_start", CancellationToken.None) : Task.CompletedTask;
    public Task StopAsync(CancellationToken token = default) => CanStop && !token.IsCancellationRequested ? PendingMutation = RunAsync("worker_stop", CancellationToken.None) : Task.CompletedTask;
    private void ApplyRoster(OperationReport report)
    {
        var roster = report.Result.GetProperty("workers").EnumerateArray().Select(row => new WorkerChoice(
            row.GetProperty("id").GetString()!, row.GetProperty("label").GetString()!,
            row.GetProperty("state").GetString()!, row.GetProperty("detail").GetString()!,
            row.GetProperty("can_start").GetBoolean(), row.GetProperty("can_stop").GetBoolean())).ToArray();
        if (roster.Any(row => row.Id is not ("01" or "02" or "03")) || roster.Select(row => row.Id).Distinct().Count() != roster.Length)
            throw new IOException("Worker status returned an invalid roster.");
        // Selection may have changed while a background read was in flight.
        // Keep both current selection and item instances whenever their IDs exist.
        var selectedId = Selected?.Id;
        foreach (var row in roster)
        {
            var existing = Workers.FirstOrDefault(worker => worker.Id == row.Id);
            if (existing is not null) existing.Update(row);
            else Workers.Add(row);
        }
        foreach (var old in Workers.Where(worker => !roster.Any(row => row.Id == worker.Id)).ToArray()) Workers.Remove(old);
        selected = Workers.FirstOrDefault(worker => worker.Id == selectedId) ?? Workers.FirstOrDefault();
    }
    private async Task RunAsync(string operation, CancellationToken token)
    {
        bool mutation = operation != "worker_list";
        if (mutation) mutating = true; else refreshing = true;
        Refresh();
        // Capture only mutation targets before awaiting. Polling must preserve
        // the user's latest selection when its response arrives.
        var targetId = mutation ? Selected?.Id : null;
        bool entered = false;
        try
        {
            await operations.WaitAsync(token); entered = true;
            var report = await client.RunAsync(operation, cancellationToken: token,
                workerId: targetId);
            if (report.Result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
            if (report.ExitCode != 0) throw new IOException("Worker operation failed. The run logs are preserved.");
            ApplyRoster(report);
            Message = report.Result.TryGetProperty("message", out var message) ? message.GetString()! :
                "Experimental worker controls. Multiplayer gameplay and checkpoint recovery are still in development.";
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception error) { Message = error.Message; }
        finally
        {
            if (entered) operations.Release();
            if (mutation) mutating = false; else refreshing = false;
            Refresh();
        }
    }
}
